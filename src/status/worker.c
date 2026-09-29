/* SPDX-License-Identifier: MIT */
#include "worker.h"
#include "source.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RAWPANEL_STATUS_IPC_SNAPSHOT 'S'
#define RAWPANEL_STATUS_IPC_CONTROL 'C'
#define RAWPANEL_STATUS_IPC_MAX (RAWPANEL_STATUS_SNAPSHOT_MAX + 1u)

static volatile sig_atomic_t terminate_requested;

static void
handle_signal(int signum)
{
  (void)signum;
  terminate_requested = 1;
}

static int
install_signals(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = handle_signal;
  if (sigaction(SIGINT, &action, NULL) < 0 ||
      sigaction(SIGTERM, &action, NULL) < 0 ||
      sigaction(SIGHUP, &action, NULL) < 0)
    return -1;
  action.sa_handler = SIG_DFL;
  if (sigaction(SIGCHLD, &action, NULL) < 0)
    return -1;
  return 0;
}

static uint64_t
monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static RawpanelStatusSource *
create_sources(const RawpanelStatusConfig *config)
{
  RawpanelStatusSource *sources;
  size_t i;

  if (!config->source_count)
    return NULL;
  sources = calloc(config->source_count, sizeof(*sources));
  if (!sources)
    return NULL;
  for (i = 0; i < config->source_count; ++i)
    rawpanel_status_source_init(&sources[i], &config->sources[i]);
  return sources;
}

static void
destroy_sources(RawpanelStatusSource *sources, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    rawpanel_status_source_destroy(&sources[i]);
  free(sources);
}

static RawpanelStatusResult
encode_snapshot(const RawpanelStatusSource *sources, size_t count, char **value,
                size_t *length)
{
  RawpanelStatusView *views;
  size_t i, n = 0;
  RawpanelStatusResult result;

  if (!count)
    return rawpanel_status_snapshot_encode(NULL, 0, value, length);
  views = calloc(count, sizeof(*views));
  if (!views)
    return RAWPANEL_STATUS_NO_MEMORY;
  for (i = 0; i < count; ++i) {
    if (!sources[i].config->enabled || !sources[i].has_value)
      continue;
    views[n].id = sources[i].config->id;
    views[n].text = sources[i].text;
    views[n].state = sources[i].state;
    ++n;
  }
  result = rawpanel_status_snapshot_encode(views, n, value, length);
  free(views);
  return result;
}

static int
publish_socket(int fd, const char *value, size_t length, FILE *err)
{
  unsigned char frame[RAWPANEL_STATUS_IPC_MAX];
  ssize_t n;

  if (length > RAWPANEL_STATUS_SNAPSHOT_MAX)
    return -1;
  frame[0] = RAWPANEL_STATUS_IPC_SNAPSHOT;
  if (length)
    memcpy(frame + 1, value, length);
  do {
    n = send(fd, frame, length + 1, MSG_NOSIGNAL);
  } while (n < 0 && errno == EINTR && !terminate_requested);
  if (n < 0) {
    if (errno != EPIPE && errno != ECONNRESET)
      fprintf(err, "rawpanel: status worker: send: %s\n", strerror(errno));
    return -1;
  }
  if ((size_t)n != length + 1) {
    fputs("rawpanel: status worker: short IPC write\n", err);
    return -1;
  }
  return 0;
}

static int
publish_stdout(FILE *out, const char *value, size_t length)
{
  if (length && fwrite(value, 1, length, out) != length)
    return -1;
  if (fputc('\n', out) == EOF || fflush(out) == EOF)
    return -1;
  return 0;
}

static int
publish_snapshot(const RawpanelStatusSource *sources, size_t count, int fd,
                 FILE *out, FILE *err)
{
  char *value = NULL;
  size_t length = 0;
  RawpanelStatusResult result;
  int rc;

  result = encode_snapshot(sources, count, &value, &length);
  if (result == RAWPANEL_STATUS_NO_MEMORY) {
    fputs("rawpanel: status worker: out of memory\n", err);
    return -1;
  }
  if (result == RAWPANEL_STATUS_TOO_LARGE) {
    fprintf(err, "rawpanel: status worker: snapshot exceeds %u bytes\n",
            RAWPANEL_STATUS_SNAPSHOT_MAX);
    return 0;
  }
  if (result != RAWPANEL_STATUS_OK) {
    fputs("rawpanel: status worker: invalid internal snapshot\n", err);
    return -1;
  }
  rc = out ? publish_stdout(out, value, length)
           : publish_socket(fd, value, length, err);
  free(value);
  return rc;
}

typedef struct {
  size_t offset;
  size_t count;
} RawpanelStatusPollSlice;

static int
handle_control(RawpanelStatusSource *sources, size_t source_count, int fd,
               bool *publish, FILE *err)
{
  unsigned char frame[2];
  RawpanelStatusSource *target = NULL;
  uint64_t now;
  ssize_t n;
  size_t i;

  n = recv(fd, frame, sizeof(frame), MSG_DONTWAIT | MSG_TRUNC);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    fprintf(err, "rawpanel: status worker: receive control: %s\n",
            strerror(errno));
    return -1;
  }
  if (n == 0)
    return 1;
  if (n != 2 || frame[0] != RAWPANEL_STATUS_IPC_CONTROL ||
      frame[1] < RAWPANEL_STATUS_CONTROL_VOLUME_UP ||
      frame[1] > RAWPANEL_STATUS_CONTROL_VOLUME_MUTE) {
    fputs("rawpanel: status worker: invalid control frame\n", err);
    return -1;
  }

  for (i = 0; i < source_count; ++i) {
    if (!sources[i].config->enabled ||
        sources[i].config->provider != RAWPANEL_STATUS_PROVIDER_ALSA)
      continue;
    if (target) {
      fputs("rawpanel: status worker: ambiguous ALSA control source\n", err);
      return -1;
    }
    target = &sources[i];
  }
  if (!target) {
    fputs("rawpanel: status worker: no ALSA control source\n", err);
    return -1;
  }
  now = monotonic_ms();
  if (!now)
    return -1;
  if (rawpanel_status_source_control(target, (RawpanelStatusControl)frame[1], now,
                                 &terminate_requested, err) > 0)
    *publish = true;
  return 0;
}

static int
wait_for_events(RawpanelStatusSource *sources, size_t source_count, int fd,
                FILE *out, int timeout, bool *publish, FILE *err)
{
  RawpanelStatusPollSlice *slices = NULL;
  struct pollfd *pfds = NULL;
  size_t base = out ? 0u : 1u;
  size_t total = base;
  size_t i;
  int rc;

  if (source_count) {
    slices = calloc(source_count, sizeof(*slices));
    if (!slices) {
      fputs("rawpanel: status worker: out of memory\n", err);
      return -1;
    }
  }
  for (i = 0; i < source_count; ++i) {
    size_t count;

    if (!sources[i].config->enabled)
      continue;
    count = rawpanel_status_source_pollfd_count(&sources[i]);
    if (count > SIZE_MAX - total) {
      fputs("rawpanel: status worker: too many source poll descriptors\n", err);
      free(slices);
      return -1;
    }
    slices[i].offset = total;
    slices[i].count = count;
    total += count;
  }

  if (total) {
    pfds = calloc(total, sizeof(*pfds));
    if (!pfds) {
      fputs("rawpanel: status worker: out of memory\n", err);
      free(slices);
      return -1;
    }
    for (i = 0; i < total; ++i)
      pfds[i].fd = -1;
  }
  if (!out) {
    pfds[0].fd = fd;
    pfds[0].events = POLLIN | POLLHUP;
  }
  for (i = 0; i < source_count; ++i) {
    if (!slices[i].count)
      continue;
    if (rawpanel_status_source_pollfds(&sources[i], &pfds[slices[i].offset],
                                   slices[i].count) < 0) {
      uint64_t now = monotonic_ms();

      slices[i].count = 0;
      if (now)
        sources[i].next_due_ms = now + sources[i].config->period_ms;
      if (timeout < 0 || (int)sources[i].config->period_ms < timeout)
        timeout = (int)sources[i].config->period_ms;
    }
  }

  rc = poll(pfds, total, timeout);
  if (rc < 0) {
    if (errno != EINTR)
      fprintf(err, "rawpanel: status worker: poll: %s\n", strerror(errno));
    free(pfds);
    free(slices);
    return errno == EINTR ? 0 : -1;
  }
  if (rc == 0) {
    free(pfds);
    free(slices);
    return 0;
  }

  if (!out) {
    if (pfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      free(pfds);
      free(slices);
      return 1;
    }
    if (pfds[0].revents & POLLIN) {
      int control_result = handle_control(sources, source_count, fd, publish, err);

      if (control_result != 0) {
        free(pfds);
        free(slices);
        return control_result;
      }
    }
  }

  for (i = 0; i < source_count; ++i) {
    bool ready = false;
    size_t j;
    int changed;
    uint64_t now;

    for (j = 0; j < slices[i].count; ++j) {
      if (pfds[slices[i].offset + j].revents) {
        ready = true;
        break;
      }
    }
    if (!ready)
      continue;
    now = monotonic_ms();
    if (!now) {
      free(pfds);
      free(slices);
      return -1;
    }
    changed = rawpanel_status_source_handle_events(
      &sources[i], &pfds[slices[i].offset], slices[i].count, now,
      &terminate_requested, err);
    if (changed > 0)
      *publish = true;
  }

  free(pfds);
  free(slices);
  return 0;
}

static int
worker_loop(const RawpanelStatusConfig *config, int fd, FILE *out, FILE *err)
{
  RawpanelStatusSource *sources = create_sources(config);
  bool publish = true;
  int rc = EXIT_SUCCESS;

  terminate_requested = 0;
  if (config->source_count && !sources) {
    fputs("rawpanel: status worker: out of memory\n", err);
    return EXIT_FAILURE;
  }
  if (install_signals() < 0) {
    fprintf(err, "rawpanel: status worker: sigaction: %s\n", strerror(errno));
    destroy_sources(sources, config->source_count);
    return EXIT_FAILURE;
  }

  while (!terminate_requested) {
    uint64_t now = monotonic_ms();
    uint64_t next = UINT64_MAX;
    size_t i;

    if (!now) {
      fputs("rawpanel: status worker: monotonic clock unavailable\n", err);
      rc = EXIT_FAILURE;
      break;
    }

    for (i = 0; i < config->source_count; ++i) {
      if (!config->sources[i].enabled)
        continue;
      if (!sources[i].next_due_ms || now >= sources[i].next_due_ms) {
        if (rawpanel_status_source_refresh(&sources[i], now,
                                       &terminate_requested, err) > 0)
          publish = true;
        if (terminate_requested)
          break;
      }
      if (sources[i].next_due_ms && sources[i].next_due_ms < next)
        next = sources[i].next_due_ms;
    }

    if (terminate_requested)
      break;

    if (publish) {
      if (publish_snapshot(sources, config->source_count, fd, out, err) < 0) {
        rc = EXIT_FAILURE;
        break;
      }
      publish = false;
    }

    if (!terminate_requested) {
      int timeout;
      int wait_result;

      now = monotonic_ms();
      if (next == UINT64_MAX)
        timeout = -1;
      else
        timeout = next <= now ? 0 : (int)(next - now);
      wait_result = wait_for_events(sources, config->source_count, fd, out,
                                    timeout, &publish, err);
      if (wait_result > 0)
        break;
      if (wait_result < 0) {
        rc = EXIT_FAILURE;
        break;
      }
    }
  }

  destroy_sources(sources, config->source_count);
  return rc;
}

void
rawpanel_status_worker_init(RawpanelStatusWorker *worker)
{
  worker->fd = -1;
  worker->pid = -1;
}

int
rawpanel_status_worker_start(RawpanelStatusWorker *worker,
                         const RawpanelStatusConfig *config, FILE *err)
{
  int sockets[2];
  pid_t pid;

  if (worker->fd >= 0 || worker->pid > 0) {
    fputs("rawpanel: status worker already running\n", err);
    return -1;
  }
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) < 0) {
    fprintf(err, "rawpanel: status worker socketpair: %s\n", strerror(errno));
    return -1;
  }
  pid = fork();
  if (pid < 0) {
    fprintf(err, "rawpanel: status worker fork: %s\n", strerror(errno));
    close(sockets[0]);
    close(sockets[1]);
    return -1;
  }
  if (pid == 0) {
    long maxfd;
    int result;

    close(sockets[0]);
    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 65536)
      maxfd = 65536;
    for (int fd = 3; fd < maxfd; ++fd)
      if (fd != sockets[1])
        close(fd);
    result = worker_loop(config, sockets[1], NULL, stderr);
    close(sockets[1]);
    _exit(result);
  }

  close(sockets[1]);
  worker->fd = sockets[0];
  worker->pid = pid;
  return 0;
}

void
rawpanel_status_worker_stop(RawpanelStatusWorker *worker, FILE *err)
{
  int status;

  if (worker->fd >= 0) {
    close(worker->fd);
    worker->fd = -1;
  }
  if (worker->pid <= 0)
    return;
  if (kill(worker->pid, SIGTERM) < 0 && errno != ESRCH)
    fprintf(err, "rawpanel: status worker kill: %s\n", strerror(errno));
  while (waitpid(worker->pid, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    if (errno != ECHILD)
      fprintf(err, "rawpanel: status worker waitpid: %s\n", strerror(errno));
    break;
  }
  worker->pid = -1;
}

int
rawpanel_status_worker_receive(RawpanelStatusWorker *worker, RawpanelStatusData *status,
                           FILE *err)
{
  unsigned char frame[RAWPANEL_STATUS_IPC_MAX];
  ssize_t n;
  RawpanelStatusData parsed = {0};
  RawpanelStatusResult result;

  if (worker->fd < 0)
    return -1;
  n = recv(worker->fd, frame, sizeof(frame), MSG_DONTWAIT | MSG_TRUNC);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    fprintf(err, "rawpanel: status worker receive: %s\n", strerror(errno));
    return -1;
  }
  if (n == 0)
    return -1;
  if ((size_t)n > sizeof(frame) || frame[0] != RAWPANEL_STATUS_IPC_SNAPSHOT) {
    fputs("rawpanel: status worker sent invalid IPC frame\n", err);
    return -1;
  }
  result = rawpanel_status_snapshot_parse(frame + 1, (size_t)n - 1, &parsed);
  if (result == RAWPANEL_STATUS_NO_MEMORY) {
    fputs("rawpanel: out of memory while receiving status snapshot\n", err);
    return -1;
  }
  if (result != RAWPANEL_STATUS_OK) {
    fputs("rawpanel: status worker sent invalid snapshot\n", err);
    return -1;
  }
  rawpanel_status_data_destroy(status);
  *status = parsed;
  return 1;
}

int
rawpanel_status_worker_control(RawpanelStatusWorker *worker,
                           RawpanelStatusControl control, FILE *err)
{
  unsigned char frame[2] = { RAWPANEL_STATUS_IPC_CONTROL, (unsigned char)control };
  ssize_t n;

  if (worker->fd < 0 || worker->pid <= 0)
    return -1;
  if (control < RAWPANEL_STATUS_CONTROL_VOLUME_UP ||
      control > RAWPANEL_STATUS_CONTROL_VOLUME_MUTE)
    return -1;
  do {
    n = send(worker->fd, frame, sizeof(frame), MSG_NOSIGNAL | MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR);
  if (n < 0) {
    if (errno != EPIPE && errno != ECONNRESET)
      fprintf(err, "rawpanel: status worker control: %s\n", strerror(errno));
    return -1;
  }
  return n == (ssize_t)sizeof(frame) ? 0 : -1;
}

int
rawpanel_status_worker_debug(const RawpanelStatusConfig *config, FILE *out, FILE *err)
{
  return worker_loop(config, -1, out, err);
}

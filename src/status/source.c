/* SPDX-License-Identifier: MIT */
#include "source.h"
#include "alsa.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SAMPLE_TEXT_MAX 1024u

static char *
xstrdup(const char *s)
{
  char *copy = strdup(s);

  if (!copy) {
    fputs("rawpanel: status worker: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return copy;
}

static const char *
proc_root(void)
{
  const char *root = getenv("RAWPANEL_STATUS_PROC_ROOT");
  return root && root[0] ? root : "/proc";
}

static const char *
sys_root(void)
{
  const char *root = getenv("RAWPANEL_STATUS_SYS_ROOT");
  return root && root[0] ? root : "/sys";
}

static int
join_parts(char *buffer, size_t size, const char *a, const char *b,
           const char *c, const char *d)
{
  const char *parts[] = {a, b, c, d};
  size_t offset = 0;

  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
    size_t length;

    if (!parts[i])
      continue;
    length = strlen(parts[i]);
    if (length >= size - offset)
      return -1;
    memcpy(buffer + offset, parts[i], length);
    offset += length;
  }
  buffer[offset] = '\0';
  return 0;
}

static int
set_sample(char **text, RawpanelStatusState *state, const char *value,
           RawpanelStatusState semantic)
{
  size_t length = strlen(value);

  if (!rawpanel_status_text_valid((const unsigned char *)value, length))
    return -1;
  free(*text);
  *text = xstrdup(value);
  *state = semantic;
  return 0;
}

static int
sample_clock(const RawpanelStatusSourceConfig *config, char **text,
             RawpanelStatusState *state)
{
  char buffer[SAMPLE_TEXT_MAX];
  struct tm local;
  time_t now = time(NULL);

  if (now == (time_t)-1 || !localtime_r(&now, &local))
    return -1;
  if (!strftime(buffer, sizeof(buffer), config->format, &local))
    return -1;
  return set_sample(text, state, buffer, RAWPANEL_STATUS_NORMAL);
}

static int
read_cpu(uint64_t *total, uint64_t *idle)
{
  char path[4096], line[4096], *save = NULL, *token;
  FILE *fp;
  uint64_t values[10] = {0};
  size_t count = 0;

  if (join_parts(path, sizeof(path), proc_root(), "/stat", NULL, NULL) < 0)
    return -1;
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  token = strtok_r(line, " \t\n", &save);
  if (!token || strcmp(token, "cpu"))
    return -1;
  while (count < sizeof(values) / sizeof(values[0]) &&
         (token = strtok_r(NULL, " \t\n", &save))) {
    char *end;
    unsigned long long value;

    errno = 0;
    value = strtoull(token, &end, 10);
    if (errno || !end || *end)
      return -1;
    values[count++] = (uint64_t)value;
  }
  if (count < 4)
    return -1;
  *total = 0;
  for (size_t i = 0; i < count; ++i)
    *total += values[i];
  *idle = values[3] + (count > 4 ? values[4] : 0);
  return *idle <= *total ? 0 : -1;
}

static int
sample_cpu(RawpanelStatusSource *source, char **text, RawpanelStatusState *state)
{
  uint64_t total, idle, delta_total, delta_idle, used;
  char buffer[64];
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;

  if (read_cpu(&total, &idle) < 0)
    return -1;
  if (!source->cpu_baseline || total <= source->cpu_total || idle < source->cpu_idle) {
    source->cpu_baseline = true;
    source->cpu_total = total;
    source->cpu_idle = idle;
    return 1;
  }
  delta_total = total - source->cpu_total;
  delta_idle = idle - source->cpu_idle;
  source->cpu_total = total;
  source->cpu_idle = idle;
  if (!delta_total || delta_idle > delta_total)
    return -1;
  used = (100u * (delta_total - delta_idle) + delta_total / 2u) / delta_total;
  if (used > 100)
    used = 100;
  if (used >= source->config->critical)
    semantic = RAWPANEL_STATUS_CRITICAL;
  else if (used >= source->config->warning)
    semantic = RAWPANEL_STATUS_WARNING;
  snprintf(buffer, sizeof(buffer), "cpu %llu%%", (unsigned long long)used);
  return set_sample(text, state, buffer, semantic);
}

static int
sample_memory(const RawpanelStatusSourceConfig *config, char **text,
              RawpanelStatusState *state)
{
  char path[4096], key[128], unit[32], buffer[64];
  FILE *fp;
  unsigned long long value, total = 0, available = 0;
  bool got_total = false, got_available = false;
  unsigned int used;
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;

  if (join_parts(path, sizeof(path), proc_root(), "/meminfo", NULL, NULL) < 0)
    return -1;
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  while (fscanf(fp, "%127s %llu %31s", key, &value, unit) == 3) {
    if (!strcmp(key, "MemTotal:")) {
      if (got_total || strcmp(unit, "kB"))
        goto invalid;
      total = value;
      got_total = true;
    } else if (!strcmp(key, "MemAvailable:")) {
      if (got_available || strcmp(unit, "kB"))
        goto invalid;
      available = value;
      got_available = true;
    }
  }
  fclose(fp);
  if (!got_total || !got_available || !total || available > total)
    return -1;
  used = (unsigned int)((100u * (total - available) + total / 2u) / total);
  if (used >= config->critical)
    semantic = RAWPANEL_STATUS_CRITICAL;
  else if (used >= config->warning)
    semantic = RAWPANEL_STATUS_WARNING;
  snprintf(buffer, sizeof(buffer), "mem %u%%", used);
  return set_sample(text, state, buffer, semantic);
invalid:
  fclose(fp);
  return -1;
}

static int
sample_filesystem(const RawpanelStatusSourceConfig *config, char **text,
                  RawpanelStatusState *state)
{
  struct statvfs fs;
  long double gib;
  unsigned long long display;
  char buffer[64];
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;
  int written;

  if (statvfs(config->path, &fs) < 0)
    return -1;
  gib = (long double)fs.f_bavail * (long double)fs.f_frsize /
        (1024.0L * 1024.0L * 1024.0L);
  if (gib > (long double)ULLONG_MAX)
    return -1;
  if (gib <= (long double)config->critical)
    semantic = RAWPANEL_STATUS_CRITICAL;
  else if (gib <= (long double)config->warning)
    semantic = RAWPANEL_STATUS_WARNING;
  if (gib < 10.0L) {
    display = (unsigned long long)(gib * 10.0L + 0.5L);
    written = snprintf(buffer, sizeof(buffer), "disk %llu.%lluG",
                       display / 10u, display % 10u);
  } else {
    display = (unsigned long long)(gib + 0.5L);
    written = snprintf(buffer, sizeof(buffer), "disk %lluG", display);
  }
  if (written < 0 || (size_t)written >= sizeof(buffer))
    return -1;
  return set_sample(text, state, buffer, semantic);
}

static int
read_uint64_file(const char *path, uint64_t *value)
{
  FILE *fp = fopen(path, "r");
  unsigned long long parsed;
  int tail;

  if (!fp)
    return -1;
  if (fscanf(fp, "%llu", &parsed) != 1) {
    fclose(fp);
    return -1;
  }
  do {
    tail = fgetc(fp);
  } while (tail == ' ' || tail == '\t' || tail == '\n' || tail == '\r');
  fclose(fp);
  if (tail != EOF)
    return -1;
  *value = (uint64_t)parsed;
  return 0;
}

static int
find_default_interface(char *buffer, size_t size)
{
  char path[4096], line[4096], iface[256], destination[32], flags[32];
  FILE *fp;

  if (join_parts(path, sizeof(path), proc_root(), "/net/route", NULL, NULL) < 0)
    return -1;
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return -1;
  }
  while (fgets(line, sizeof(line), fp)) {
    unsigned long parsed_flags;
    char *end;

    if (sscanf(line, "%255s %31s %*s %31s", iface, destination, flags) != 3)
      continue;
    errno = 0;
    parsed_flags = strtoul(flags, &end, 16);
    if (errno || !end || *end)
      continue;
    if (!strcmp(destination, "00000000") && (parsed_flags & 0x1u)) {
      if (join_parts(buffer, size, iface, NULL, NULL, NULL) < 0) {
        fclose(fp);
        return -1;
      }
      fclose(fp);
      return 0;
    }
  }
  fclose(fp);
  return -1;
}

static int
format_rate(char *buffer, size_t size, uint64_t rate)
{
  unsigned long long value = (unsigned long long)rate;
  int written;

  if (rate >= 1048576u) {
    unsigned long long whole = value / 1048576u;
    unsigned long long tenth = (value % 1048576u) * 10u / 1048576u;

    written = snprintf(buffer, size, "%llu.%lluM", whole, tenth);
  } else if (rate >= 1024u) {
    written = snprintf(buffer, size, "%lluK", value / 1024u);
  } else {
    written = snprintf(buffer, size, "%lluB", value);
  }
  return written >= 0 && (size_t)written < size ? 0 : -1;
}

static uint64_t
rate_per_second(uint64_t delta, uint64_t elapsed_ms)
{
  long double rate;

  rate = (long double)delta * 1000.0L / (long double)elapsed_ms;
  if (rate >= (long double)UINT64_MAX)
    return UINT64_MAX;
  return (uint64_t)rate;
}

static int
sample_network(RawpanelStatusSource *source, uint64_t now_ms, char **text,
               RawpanelStatusState *state)
{
  char iface[IF_NAMESIZE], rx_path[4096], tx_path[4096];
  char rx_text[32], tx_text[32];
  char buffer[128];
  uint64_t rx, tx, dt, rx_rate, tx_rate;

  if (!strcmp(source->config->interface, "default")) {
    if (find_default_interface(iface, sizeof(iface)) < 0)
      return -1;
  } else if (join_parts(iface, sizeof(iface), source->config->interface, NULL,
                        NULL, NULL) < 0) {
    return -1;
  }
  if (join_parts(rx_path, sizeof(rx_path), sys_root(), "/class/net/", iface,
                 "/statistics/rx_bytes") < 0 ||
      join_parts(tx_path, sizeof(tx_path), sys_root(), "/class/net/", iface,
                 "/statistics/tx_bytes") < 0)
    return -1;
  if (read_uint64_file(rx_path, &rx) < 0 || read_uint64_file(tx_path, &tx) < 0)
    return -1;

  if (!source->network_baseline || !source->network_interface ||
      strcmp(source->network_interface, iface) || now_ms <= source->network_time_ms ||
      rx < source->network_rx || tx < source->network_tx) {
    free(source->network_interface);
    source->network_interface = xstrdup(iface);
    source->network_rx = rx;
    source->network_tx = tx;
    source->network_time_ms = now_ms;
    source->network_baseline = true;
    if (join_parts(buffer, sizeof(buffer), "net ", iface, NULL, NULL) < 0)
      return -1;
    return set_sample(text, state, buffer, RAWPANEL_STATUS_NORMAL);
  }

  dt = now_ms - source->network_time_ms;
  rx_rate = rate_per_second(rx - source->network_rx, dt);
  tx_rate = rate_per_second(tx - source->network_tx, dt);
  source->network_rx = rx;
  source->network_tx = tx;
  source->network_time_ms = now_ms;
  if (format_rate(rx_text, sizeof(rx_text), rx_rate) < 0 ||
      format_rate(tx_text, sizeof(tx_text), tx_rate) < 0)
    return -1;
  snprintf(buffer, sizeof(buffer), "net ↓%s ↑%s", rx_text, tx_text);
  return set_sample(text, state, buffer, RAWPANEL_STATUS_NORMAL);
}

static int
sample_battery(const RawpanelStatusSourceConfig *config, char **text,
               RawpanelStatusState *state)
{
  char base[4096], path[4096], status[64] = "", buffer[64];
  DIR *dir;
  struct dirent *entry;
  uint64_t capacity;

  if (join_parts(base, sizeof(base), sys_root(), "/class/power_supply", NULL,
                 NULL) < 0)
    return -1;
  dir = opendir(base);
  if (!dir)
    return -1;
  while ((entry = readdir(dir))) {
    FILE *fp;
    const char *suffix = "";
    RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;

    if (strncmp(entry->d_name, "BAT", 3))
      continue;
    if (join_parts(path, sizeof(path), base, "/", entry->d_name,
                   "/capacity") < 0)
      continue;
    if (read_uint64_file(path, &capacity) < 0 || capacity > 100)
      continue;
    if (join_parts(path, sizeof(path), base, "/", entry->d_name, "/status") < 0)
      continue;
    fp = fopen(path, "r");
    if (fp) {
      if (!fgets(status, sizeof(status), fp))
        status[0] = '\0';
      fclose(fp);
      status[strcspn(status, "\r\n")] = '\0';
    }
    if (!strcmp(status, "Charging"))
      suffix = "+";
    else if (!strcmp(status, "Full"))
      suffix = "=";
    else if (capacity <= config->critical)
      semantic = RAWPANEL_STATUS_CRITICAL;
    else if (capacity <= config->warning)
      semantic = RAWPANEL_STATUS_WARNING;
    snprintf(buffer, sizeof(buffer), "bat %llu%%%s",
             (unsigned long long)capacity, suffix);
    closedir(dir);
    return set_sample(text, state, buffer, semantic);
  }
  closedir(dir);
  return -1;
}

static int
sample_alsa(RawpanelStatusSource *source, char **text, RawpanelStatusState *state,
            FILE *err)
{
  char buffer[64];

  if (rawpanel_status_alsa_sample(source->config, buffer, sizeof(buffer),
                                  state, err) < 0)
    return -1;
  return set_sample(text, state, buffer, *state);
}

static uint64_t
monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int
parse_exec_line(char *line, char **text, RawpanelStatusState *state)
{
  char *tab = strchr(line, '\t');
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;
  const char *payload = line;

  line[strcspn(line, "\r\n")] = '\0';
  if (!line[0])
    return -1;
  if (tab) {
    *tab = '\0';
    if (!rawpanel_status_state_parse((const unsigned char *)line, strlen(line),
                                 &semantic) || semantic == RAWPANEL_STATUS_STALE)
      return -1;
    payload = tab + 1;
  }
  return set_sample(text, state, payload, semantic);
}

static void
terminate_exec_group(pid_t pid, int *status)
{
  struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };

  (void)kill(-pid, SIGTERM);
  while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
    ;
  (void)kill(-pid, SIGKILL);
  while (waitpid(pid, status, 0) < 0 && errno == EINTR)
    ;
}

static int
sample_exec(const RawpanelStatusSourceConfig *config, char **text,
            RawpanelStatusState *state, const volatile sig_atomic_t *terminate)
{
  int pipefd[2], status = 0;
  pid_t pid;
  char buffer[SAMPLE_TEXT_MAX + 1];
  size_t used = 0;
  uint64_t deadline;
  bool exited = false, eof = false;

  if (pipe(pipefd) < 0)
    return -1;
  pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    int devnull;

    setpgid(0, 0);
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0)
      _exit(126);
    close(pipefd[1]);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      if (devnull != STDERR_FILENO)
        close(devnull);
    }
    execvp(config->argv[0], config->argv);
    _exit(errno == ENOENT ? 127 : 126);
  }
  close(pipefd[1]);
  setpgid(pid, pid);
  fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);
  deadline = monotonic_ms() + config->timeout_ms;

  while (!eof || !exited) {
    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN | POLLHUP };
    uint64_t now = monotonic_ms();
    int timeout, rc;

    if (terminate && *terminate) {
      terminate_exec_group(pid, &status);
      close(pipefd[0]);
      return -1;
    }
    if (now >= deadline) {
      (void)kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
      close(pipefd[0]);
      return -1;
    }
    timeout = (int)(deadline - now);
    if (timeout > 100)
      timeout = 100;
    rc = poll(&pfd, 1, timeout);
    if (rc < 0 && errno != EINTR) {
      (void)kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
      close(pipefd[0]);
      return -1;
    }
    if (rc > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
      for (;;) {
        ssize_t n = read(pipefd[0], buffer + used, SAMPLE_TEXT_MAX - used);
        if (n > 0) {
          used += (size_t)n;
          if (memchr(buffer, '\n', used) || used == SAMPLE_TEXT_MAX)
            break;
          continue;
        }
        if (n == 0)
          eof = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          eof = true;
        break;
      }
    }
    if (!exited) {
      pid_t done = waitpid(pid, &status, WNOHANG);
      if (done == pid)
        exited = true;
    }
    if (memchr(buffer, '\n', used) && exited)
      eof = true;
  }
  close(pipefd[0]);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !used)
    return -1;
  if (used == SAMPLE_TEXT_MAX && !memchr(buffer, '\n', used))
    return -1;
  buffer[used] = '\0';
  return parse_exec_line(buffer, text, state);
}

void
rawpanel_status_source_init(RawpanelStatusSource *source,
                     const RawpanelStatusSourceConfig *config)
{
  memset(source, 0, sizeof(*source));
  source->config = config;
}

void
rawpanel_status_source_destroy(RawpanelStatusSource *source)
{
  free(source->text);
  free(source->network_interface);
  memset(source, 0, sizeof(*source));
}

int
rawpanel_status_source_refresh(RawpanelStatusSource *source, uint64_t now_ms,
                           const volatile sig_atomic_t *terminate, FILE *err)
{
  char *text = NULL;
  RawpanelStatusState state = RAWPANEL_STATUS_NORMAL;
  int rc = -1;

  switch (source->config->provider) {
  case RAWPANEL_STATUS_PROVIDER_ALSA:
    rc = sample_alsa(source, &text, &state, err);
    break;
  case RAWPANEL_STATUS_PROVIDER_BATTERY:
    rc = sample_battery(source->config, &text, &state);
    break;
  case RAWPANEL_STATUS_PROVIDER_CLOCK:
    rc = sample_clock(source->config, &text, &state);
    break;
  case RAWPANEL_STATUS_PROVIDER_CPU:
    rc = sample_cpu(source, &text, &state);
    break;
  case RAWPANEL_STATUS_PROVIDER_EXEC:
    rc = sample_exec(source->config, &text, &state, terminate);
    break;
  case RAWPANEL_STATUS_PROVIDER_FILESYSTEM:
    rc = sample_filesystem(source->config, &text, &state);
    break;
  case RAWPANEL_STATUS_PROVIDER_MEMORY:
    rc = sample_memory(source->config, &text, &state);
    break;
  case RAWPANEL_STATUS_PROVIDER_NETWORK:
    rc = sample_network(source, now_ms, &text, &state);
    break;
  }

  source->next_due_ms = now_ms + source->config->period_ms;
  if (rc == 0) {
    free(source->text);
    source->text = text;
    source->state = state;
    source->has_value = true;
    return 1;
  }
  free(text);
  if (terminate && *terminate)
    return 0;
  if (rc == 1)
    return 0;
  if (source->has_value && source->state != RAWPANEL_STATUS_STALE) {
    source->state = RAWPANEL_STATUS_STALE;
    fprintf(err, "rawpanel: status worker: source '%s' refresh failed; marking stale\n",
            source->config->id);
    return 1;
  }
  if (!source->has_value)
    fprintf(err, "rawpanel: status worker: source '%s' refresh failed\n", source->config->id);
  return 0;
}

int
rawpanel_status_source_control(RawpanelStatusSource *source,
                               RawpanelStatusControl control, uint64_t now_ms,
                               const volatile sig_atomic_t *terminate, FILE *err)
{
  int changed = 0;

  if (!source || source->config->provider != RAWPANEL_STATUS_PROVIDER_ALSA)
    return -1;
  if (rawpanel_status_alsa_control(source->config, control, err) < 0) {
    source->next_due_ms = now_ms + source->config->period_ms;
    if (source->has_value && source->state != RAWPANEL_STATUS_STALE) {
      source->state = RAWPANEL_STATUS_STALE;
      changed = 1;
    }
    return changed;
  }
  return rawpanel_status_source_refresh(source, now_ms, terminate, err);
}

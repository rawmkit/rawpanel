#include "status/worker.h"
#include "status-alsa-fake.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "status worker control test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

static void
wait_packet(int fd)
{
  struct pollfd pfd = { .fd = fd, .events = POLLIN };

  if (poll(&pfd, 1, 1000) <= 0)
    fail("timed out waiting for worker snapshot");
}

static void
expect_text(RawpanelStatusWorker *worker, const char *expected)
{
  RawpanelStatusData status = {0};

  wait_packet(worker->fd);
  if (rawpanel_status_worker_receive(worker, &status, stderr) != 1)
    fail("worker snapshot could not be received");
  if (status.count != 1 || strcmp(status.segments[0].text, expected))
    fail("worker snapshot text differs");
  rawpanel_status_data_destroy(&status);
}

int
main(void)
{
  RawpanelStatusSourceConfig source = {
    .id = "volume",
    .enabled = true,
    .provider = RAWPANEL_STATUS_PROVIDER_ALSA,
    .period_ms = 5000,
    .step = 10,
    .device = "default",
    .control = "Master"
  };
  RawpanelStatusConfig config = { .sources = &source, .source_count = 1 };
  RawpanelStatusWorker worker;

  rawpanel_test_alsa_reset();
  rawpanel_test_alsa_set_poll_count(0);

  rawpanel_status_worker_init(&worker);
  if (rawpanel_status_worker_start(&worker, &config, stderr) < 0)
    fail("worker could not start");
  expect_text(&worker, "vol 75%");

  if (rawpanel_status_worker_control(&worker,
                                 RAWPANEL_STATUS_CONTROL_VOLUME_DOWN, stderr) < 0)
    fail("volume-down control could not be sent");
  expect_text(&worker, "vol 65%");

  if (rawpanel_status_worker_control(&worker,
                                 RAWPANEL_STATUS_CONTROL_VOLUME_MUTE, stderr) < 0)
    fail("mute control could not be sent");
  expect_text(&worker, "vol mute");

  rawpanel_status_worker_stop(&worker, stderr);
  return EXIT_SUCCESS;
}

#include "status/alsa.h"
#include "status-alsa-fake.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "status ALSA test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

int
main(void)
{
  RawpanelStatusSourceConfig config = {0};
  RawpanelStatusAlsa *state = NULL;
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;
  struct pollfd pfd = {0};
  char buffer[64];

  rawpanel_test_alsa_reset();

  config.device = "default";
  config.control = "Master";
  config.step = 10;

  if (rawpanel_status_alsa_sample(&state, &config, buffer, sizeof(buffer),
                              &semantic, stderr) < 0)
    fail("normal sample failed");
  if (strcmp(buffer, "vol 75%") || semantic != RAWPANEL_STATUS_NORMAL)
    fail("multi-channel volume was not averaged");

  if (rawpanel_status_alsa_control(&state, &config,
                               RAWPANEL_STATUS_CONTROL_VOLUME_DOWN, stderr) < 0)
    fail("volume-down control failed");
  if (rawpanel_test_alsa_left_volume() != 40 ||
      rawpanel_test_alsa_right_volume() != 90)
    fail("volume-down control did not preserve channel balance");

  if (rawpanel_status_alsa_control(&state, &config,
                               RAWPANEL_STATUS_CONTROL_VOLUME_UP, stderr) < 0)
    fail("volume-up control failed");
  if (rawpanel_test_alsa_left_volume() != 50 ||
      rawpanel_test_alsa_right_volume() != 100)
    fail("volume-up control did not restore channel values");

  if (rawpanel_status_alsa_control(&state, &config,
                               RAWPANEL_STATUS_CONTROL_VOLUME_MUTE, stderr) < 0 ||
      !rawpanel_test_alsa_muted())
    fail("mute control did not disable playback switches");

  if (rawpanel_status_alsa_sample(&state, &config, buffer, sizeof(buffer),
                              &semantic, stderr) < 0)
    fail("muted sample failed");
  if (strcmp(buffer, "vol mute") || semantic != RAWPANEL_STATUS_WARNING)
    fail("mute did not produce warning semantics");

  if (rawpanel_status_alsa_control(&state, &config,
                               RAWPANEL_STATUS_CONTROL_VOLUME_MUTE, stderr) < 0 ||
      rawpanel_test_alsa_muted())
    fail("mute control did not restore playback switches");

  if (rawpanel_status_alsa_pollfd_count(state) != 1)
    fail("mixer poll descriptor count changed");
  if (rawpanel_status_alsa_pollfds(state, &pfd, 1) < 0 ||
      pfd.fd != 7 || !(pfd.events & POLLIN))
    fail("mixer poll descriptor was not exported");

  pfd.revents = POLLIN;
  if (rawpanel_status_alsa_handle_events(&state, &pfd, 1, stderr) != 1 ||
      rawpanel_test_alsa_handled_events() != 1)
    fail("mixer input event was not handled");

  pfd.revents = POLLHUP;
  if (rawpanel_status_alsa_handle_events(&state, &pfd, 1, stderr) != -1 || state)
    fail("mixer hangup did not invalidate provider state");

  return EXIT_SUCCESS;
}

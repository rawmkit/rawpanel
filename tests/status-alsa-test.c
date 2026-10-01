#include "status/alsa.h"
#include "status-alsa-fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "status ALSA test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

static void
expect_released(unsigned int expected_opens)
{
  if (rawpanel_test_alsa_mixer_open())
    fail("mixer handle escaped an ALSA operation");
  if (rawpanel_test_alsa_open_count() != expected_opens ||
      rawpanel_test_alsa_close_count() != expected_opens)
    fail("mixer open/close lifetime is unbalanced");
}

int
main(void)
{
  RawpanelStatusSourceConfig config = {0};
  RawpanelStatusState semantic = RAWPANEL_STATUS_NORMAL;
  char buffer[64];
  unsigned int operations = 0;

  rawpanel_test_alsa_reset();

  config.device = "default";
  config.control = "Master";
  config.step = 10;

  if (rawpanel_status_alsa_sample(&config, buffer, sizeof(buffer),
                                  &semantic, stderr) < 0)
    fail("normal sample failed");
  expect_released(++operations);
  if (strcmp(buffer, "vol 75%") || semantic != RAWPANEL_STATUS_NORMAL)
    fail("multi-channel volume was not averaged");

  if (rawpanel_status_alsa_control(&config,
                                   RAWPANEL_STATUS_CONTROL_VOLUME_DOWN,
                                   stderr) < 0)
    fail("volume-down control failed");
  expect_released(++operations);
  if (rawpanel_test_alsa_left_volume() != 40 ||
      rawpanel_test_alsa_right_volume() != 90)
    fail("volume-down control did not preserve channel balance");

  if (rawpanel_status_alsa_control(&config,
                                   RAWPANEL_STATUS_CONTROL_VOLUME_UP,
                                   stderr) < 0)
    fail("volume-up control failed");
  expect_released(++operations);
  if (rawpanel_test_alsa_left_volume() != 50 ||
      rawpanel_test_alsa_right_volume() != 100)
    fail("volume-up control did not restore channel values");

  if (rawpanel_status_alsa_control(&config,
                                   RAWPANEL_STATUS_CONTROL_VOLUME_MUTE,
                                   stderr) < 0 ||
      !rawpanel_test_alsa_muted())
    fail("mute control did not disable playback switches");
  expect_released(++operations);

  if (rawpanel_status_alsa_sample(&config, buffer, sizeof(buffer),
                                  &semantic, stderr) < 0)
    fail("muted sample failed");
  expect_released(++operations);
  if (strcmp(buffer, "vol mute") || semantic != RAWPANEL_STATUS_WARNING)
    fail("mute did not produce warning semantics");

  if (rawpanel_status_alsa_control(&config,
                                   RAWPANEL_STATUS_CONTROL_VOLUME_MUTE,
                                   stderr) < 0 ||
      rawpanel_test_alsa_muted())
    fail("mute control did not restore playback switches");
  expect_released(++operations);

  rawpanel_test_alsa_set_attach_failure(1);
  if (rawpanel_status_alsa_sample(&config, buffer, sizeof(buffer),
                                  &semantic, stderr) >= 0)
    fail("attach failure was accepted");
  expect_released(++operations);

  return EXIT_SUCCESS;
}

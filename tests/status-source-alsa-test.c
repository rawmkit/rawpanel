#include "status/source.h"
#include "status-alsa-fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "status source ALSA test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

int
main(void)
{
  RawpanelStatusSourceConfig config = {
    .id = "volume",
    .enabled = true,
    .provider = RAWPANEL_STATUS_PROVIDER_ALSA,
    .period_ms = 5000,
    .step = 10,
    .device = "default",
    .control = "Master"
  };
  RawpanelStatusSource source;

  rawpanel_test_alsa_reset();
  rawpanel_status_source_init(&source, &config);

  if (rawpanel_status_source_refresh(&source, 1000, NULL, stderr) != 1)
    fail("initial refresh failed");
  if (!source.has_value || strcmp(source.text, "vol 75%"))
    fail("initial ALSA value differs");
  if (source.next_due_ms != 6000)
    fail("ALSA source did not retain periodic scheduling");
  if (rawpanel_test_alsa_mixer_open() ||
      rawpanel_test_alsa_open_count() != rawpanel_test_alsa_close_count())
    fail("refresh retained an ALSA mixer handle");

  if (rawpanel_status_source_control(&source,
                                     RAWPANEL_STATUS_CONTROL_VOLUME_DOWN,
                                     2000, NULL, stderr) != 1)
    fail("volume control failed");
  if (strcmp(source.text, "vol 65%"))
    fail("control did not refresh the source value");
  if (source.next_due_ms != 7000)
    fail("control refresh did not preserve periodic scheduling");
  if (rawpanel_test_alsa_mixer_open() ||
      rawpanel_test_alsa_open_count() != rawpanel_test_alsa_close_count())
    fail("control retained an ALSA mixer handle");

  rawpanel_status_source_destroy(&source);
  return EXIT_SUCCESS;
}

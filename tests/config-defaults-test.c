/* SPDX-License-Identifier: MIT */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "rawpanel config defaults test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

static int
string_equal(const char *a, const char *b)
{
  if (!a || !b)
    return a == b;
  return strcmp(a, b) == 0;
}

static int
command_equal(const RawpanelStatusSourceConfig *a,
              const RawpanelStatusSourceConfig *b)
{
  size_t i;

  if (a->argc != b->argc)
    return 0;
  for (i = 0; i < a->argc; ++i)
    if (strcmp(a->argv[i], b->argv[i]))
      return 0;
  return 1;
}

static int
source_equal(const RawpanelStatusSourceConfig *a,
             const RawpanelStatusSourceConfig *b)
{
  return string_equal(a->id, b->id) &&
         a->enabled == b->enabled &&
         a->provider == b->provider &&
         a->period_ms == b->period_ms &&
         a->warning == b->warning &&
         a->critical == b->critical &&
         a->step == b->step &&
         string_equal(a->device, b->device) &&
         string_equal(a->control, b->control) &&
         string_equal(a->format, b->format) &&
         string_equal(a->path, b->path) &&
         string_equal(a->interface, b->interface) &&
         a->timeout_ms == b->timeout_ms &&
         command_equal(a, b);
}

int
main(int argc, char **argv)
{
  RawpanelConfig defaults;
  RawpanelConfig loaded;
  size_t i, j;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s RAWPANEL.CONF\n", argv[0]);
    return EXIT_FAILURE;
  }

  rawpanel_config_init(&defaults);
  rawpanel_config_init(&loaded);
  if (rawpanel_config_load(&loaded, argv[1], stderr) < 0)
    fail("canonical configuration did not load");

#define EXPECT(expr, message) do { if (!(expr)) fail(message); } while (0)
  EXPECT(string_equal(defaults.font, loaded.font), "font differs");
  EXPECT(defaults.top == loaded.top, "position differs");
  EXPECT(defaults.height == loaded.height, "height differs");
  EXPECT(defaults.padding == loaded.padding, "padding differs");
  EXPECT(defaults.item_spacing == loaded.item_spacing, "item spacing differs");
  EXPECT(defaults.section_spacing == loaded.section_spacing,
         "section spacing differs");
  EXPECT(defaults.cell_margin == loaded.cell_margin, "cell margin differs");
  EXPECT(defaults.indicator_size == loaded.indicator_size,
         "indicator size differs");
  EXPECT(defaults.tags_all == loaded.tags_all, "tag visibility differs");
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
      EXPECT(string_equal(defaults.colors[i][j], loaded.colors[i][j]),
             "color differs");
  EXPECT(defaults.systray_enabled == loaded.systray_enabled,
         "systray enabled differs");
  EXPECT(defaults.systray_spacing == loaded.systray_spacing,
         "systray spacing differs");
  EXPECT(defaults.systray_icon_size == loaded.systray_icon_size,
         "systray icon size differs");
  EXPECT(defaults.status_enabled == loaded.status_enabled,
         "status enabled differs");
  EXPECT(string_equal(defaults.status_separator, loaded.status_separator),
         "status separator differs");
  EXPECT(defaults.status.source_count == loaded.status.source_count,
         "status source count differs");
  for (i = 0; i < defaults.status.source_count; ++i)
    EXPECT(source_equal(&defaults.status.sources[i], &loaded.status.sources[i]),
           "status source differs");
  EXPECT(defaults.button_count == loaded.button_count, "button count differs");
#undef EXPECT

  rawpanel_config_destroy(&loaded);
  rawpanel_config_destroy(&defaults);
  puts("rawpanel config defaults: ok");
  return EXIT_SUCCESS;
}

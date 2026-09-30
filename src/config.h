/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_CONFIG_H
#define RAWPANEL_CONFIG_H

#include "status/config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RAWPANEL_SCHEME_COUNT 5
#define RAWPANEL_COLOR_COUNT 3
#define RAWPANEL_MAX_BUTTONS 64
#define RAWPANEL_MAX_ARGV 32

typedef enum {
  RAWPANEL_SCHEME_NORMAL,
  RAWPANEL_SCHEME_SELECTED,
  RAWPANEL_SCHEME_FLOATING,
  RAWPANEL_SCHEME_URGENT,
  RAWPANEL_SCHEME_ERROR
} RawpanelScheme;

typedef enum {
  RAWPANEL_COLOR_BORDER,
  RAWPANEL_COLOR_FOREGROUND,
  RAWPANEL_COLOR_BACKGROUND
} RawpanelColor;

typedef enum {
  RAWPANEL_TARGET_TAGS,
  RAWPANEL_TARGET_LAYOUT,
  RAWPANEL_TARGET_TITLE,
  RAWPANEL_TARGET_STATUS
} RawpanelTarget;

typedef enum {
  RAWPANEL_ACTION_NONE,
  RAWPANEL_ACTION_RAWM,
  RAWPANEL_ACTION_VOLUME_UP,
  RAWPANEL_ACTION_VOLUME_DOWN,
  RAWPANEL_ACTION_VOLUME_MUTE,
  RAWPANEL_ACTION_EXEC
} RawpanelActionType;

typedef struct {
  RawpanelTarget target;
  char *status_id;
  unsigned int modifiers;
  unsigned int button;
  RawpanelActionType type;
  char *command;
  int number;
  bool has_number;
  unsigned int tag;
  bool has_tag;
  char *layout;
  char **argv;
  size_t argc;
} RawpanelButton;

typedef struct {
  char *font;
  char *colors[RAWPANEL_SCHEME_COUNT][RAWPANEL_COLOR_COUNT];
  bool top;
  unsigned int height;
  unsigned int padding;
  unsigned int item_spacing;
  unsigned int section_spacing;
  unsigned int cell_margin;
  unsigned int indicator_size;
  bool tags_all;
  bool systray_enabled;
  unsigned int systray_spacing;
  unsigned int systray_icon_size;
  bool status_enabled;
  char *status_separator;
  RawpanelStatusConfig status;
  RawpanelButton *buttons;
  size_t button_count;
} RawpanelConfig;

void rawpanel_config_init(RawpanelConfig *cfg);
void rawpanel_config_destroy(RawpanelConfig *cfg);
int rawpanel_config_load(RawpanelConfig *cfg, const char *path, FILE *err);

#endif

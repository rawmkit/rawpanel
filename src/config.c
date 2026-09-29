/* SPDX-License-Identifier: MIT */
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *dupstr(const char *s)
{
  size_t n;
  char *p;
  if (!s)
    return NULL;
  n = strlen(s) + 1;
  p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

static void free_button(RawpanelButton *b)
{
  size_t i;
  if (!b)
    return;
  free(b->status_id);
  free(b->command);
  free(b->layout);
  for (i = 0; i < b->argc; ++i)
    free(b->argv[i]);
  free(b->argv);
  memset(b, 0, sizeof(*b));
}

void rawpanel_config_init(RawpanelConfig *cfg)
{
  static const char *const defaults[RAWPANEL_SCHEME_COUNT][RAWPANEL_COLOR_COUNT] = {
    {"#444444", "#bbbbbb", "#222222"},
    {"#005577", "#eeeeee", "#005577"},
    {"#d79921", "#eeeeee", "#3c3836"},
    {"#ff0000", "#ffffff", "#770000"},
    {"#cc241d", "#ffffff", "#770000"},
  };
  size_t i, j;

  memset(cfg, 0, sizeof(*cfg));
  cfg->font = dupstr("monospace:size=10");
  cfg->height = 24;
  cfg->padding = 6;
  cfg->item_spacing = 4;
  cfg->section_spacing = 10;
  cfg->cell_margin = 4;
  cfg->indicator_size = 2;
  cfg->top = true;
  cfg->systray_enabled = true;
  cfg->systray_spacing = 4;
  cfg->systray_icon_size = 20;
  cfg->status_enabled = true;
  cfg->status_separator = dupstr("·");
  rawpanel_status_config_init(&cfg->status);
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
      cfg->colors[i][j] = dupstr(defaults[i][j]);
}

void rawpanel_config_destroy(RawpanelConfig *cfg)
{
  size_t i, j;
  if (!cfg)
    return;
  free(cfg->font);
  free(cfg->status_separator);
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
      free(cfg->colors[i][j]);
  for (i = 0; i < cfg->button_count; ++i)
    free_button(&cfg->buttons[i]);
  free(cfg->buttons);
  rawpanel_status_config_destroy(&cfg->status);
  memset(cfg, 0, sizeof(*cfg));
}

static int replace_string(char **dst, const char *src)
{
  char *copy = dupstr(src);
  if (!copy)
    return -1;
  free(*dst);
  *dst = copy;
  return 0;
}

static int parse_color_group(RawpanelConfig *cfg, const config_setting_t *colors,
                             FILE *err)
{
  static const char *const schemes[] = {"normal", "selected", "floating", "urgent", "error"};
  static const char *const components[] = {"border", "foreground", "background"};
  size_t i, j;

  if (!colors)
    return 0;
  if (config_setting_type(colors) != CONFIG_TYPE_GROUP)
    goto bad;
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i) {
    const config_setting_t *scheme = config_setting_get_member(colors, schemes[i]);
    if (!scheme)
      continue;
    if (config_setting_type(scheme) != CONFIG_TYPE_GROUP)
      goto bad;
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j) {
      const char *value = NULL;
      if (config_setting_lookup_string(scheme, components[j], &value)) {
        if (!value || !*value || replace_string(&cfg->colors[i][j], value) < 0)
          return -1;
      }
    }
  }
  return 0;
bad:
  if (err)
    fputs("rawpanel: invalid colors configuration\n", err);
  return -1;
}

static unsigned int parse_modifiers(const config_setting_t *setting, FILE *err)
{
  unsigned int result = 0;
  int i, n;
  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
      config_setting_type(setting) != CONFIG_TYPE_ARRAY)
    goto bad;
  n = config_setting_length(setting);
  for (i = 0; i < n; ++i) {
    const char *s = config_setting_get_string_elem(setting, i);
    if (!s)
      goto bad;
    if (!strcmp(s, "Shift")) result |= 1u << 0;
    else if (!strcmp(s, "Control")) result |= 1u << 1;
    else if (!strcmp(s, "Mod1")) result |= 1u << 2;
    else if (!strcmp(s, "Mod4")) result |= 1u << 3;
    else goto bad;
  }
  return result;
bad:
  if (err)
    fputs("rawpanel: invalid button modifiers\n", err);
  return UINT_MAX;
}

static int parse_target(const char *s, RawpanelTarget *target)
{
  if (!strcmp(s, "tag_bar")) *target = RAWPANEL_TARGET_TAGS;
  else if (!strcmp(s, "layout")) *target = RAWPANEL_TARGET_LAYOUT;
  else if (!strcmp(s, "title")) *target = RAWPANEL_TARGET_TITLE;
  else if (!strcmp(s, "status")) *target = RAWPANEL_TARGET_STATUS;
  else return -1;
  return 0;
}

static int parse_action(const char *s, RawpanelActionType *type, char **command)
{
  if (!strcmp(s, "volume_up")) *type = RAWPANEL_ACTION_VOLUME_UP;
  else if (!strcmp(s, "volume_down")) *type = RAWPANEL_ACTION_VOLUME_DOWN;
  else if (!strcmp(s, "volume_mute")) *type = RAWPANEL_ACTION_VOLUME_MUTE;
  else if (!strcmp(s, "exec")) *type = RAWPANEL_ACTION_EXEC;
  else {
    *type = RAWPANEL_ACTION_RAWM;
    *command = dupstr(s);
    if (!*command)
      return -1;
  }
  return 0;
}

static int parse_argv(const config_setting_t *setting, char ***argv, size_t *argc,
                      FILE *err)
{
  int i, n;
  char **out;
  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
      config_setting_type(setting) != CONFIG_TYPE_ARRAY)
    goto bad;
  n = config_setting_length(setting);
  if (n <= 0 || n > RAWPANEL_MAX_ARGV)
    goto bad;
  out = calloc((size_t)n + 1u, sizeof(*out));
  if (!out)
    return -1;
  for (i = 0; i < n; ++i) {
    const char *s = config_setting_get_string_elem(setting, i);
    if (!s || !*s || !(out[i] = dupstr(s))) {
      while (i-- > 0) free(out[i]);
      free(out);
      goto bad;
    }
  }
  *argv = out;
  *argc = (size_t)n;
  return 0;
bad:
  if (err)
    fputs("rawpanel: invalid exec argv\n", err);
  return -1;
}

static int parse_buttons(RawpanelConfig *cfg, const config_setting_t *list, FILE *err)
{
  int i, n;
  RawpanelButton *buttons;

  if (!list)
    return 0;
  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    goto bad;
  n = config_setting_length(list);
  if (n < 0 || n > RAWPANEL_MAX_BUTTONS)
    goto bad;
  buttons = calloc((size_t)n, sizeof(*buttons));
  if (n && !buttons)
    return -1;

  for (i = 0; i < n; ++i) {
    config_setting_t *item = config_setting_get_elem(list, i);
    const char *target = NULL, *action = NULL, *id = NULL, *layout = NULL;
    int button = 0, number = 0, tag = 0;
    config_setting_t *mods, *argv;
    unsigned int parsed_mods;

    if (!item || config_setting_type(item) != CONFIG_TYPE_GROUP ||
        !config_setting_lookup_string(item, "target", &target) ||
        !config_setting_lookup_int(item, "button", &button) ||
        !config_setting_lookup_string(item, "action", &action) ||
        button < 1 || button > 5 || parse_target(target, &buttons[i].target) < 0 ||
        parse_action(action, &buttons[i].type, &buttons[i].command) < 0)
      goto fail;

    mods = config_setting_get_member(item, "modifiers");
    parsed_mods = parse_modifiers(mods, err);
    if (parsed_mods == UINT_MAX)
      goto fail;
    buttons[i].modifiers = parsed_mods;
    buttons[i].button = (unsigned int)button;

    if (config_setting_lookup_string(item, "id", &id)) {
      buttons[i].status_id = dupstr(id);
      if (!buttons[i].status_id)
        goto fail;
    }
    if (config_setting_lookup_int(item, "number", &number)) {
      buttons[i].has_number = true;
      buttons[i].number = number;
    }
    if (config_setting_lookup_int(item, "tag", &tag)) {
      if (tag < 1 || tag > 32)
        goto fail;
      buttons[i].has_tag = true;
      buttons[i].tag = (unsigned int)tag;
    }
    if (config_setting_lookup_string(item, "layout", &layout)) {
      buttons[i].layout = dupstr(layout);
      if (!buttons[i].layout)
        goto fail;
    }
    argv = config_setting_get_member(item, "argv");
    if (buttons[i].type == RAWPANEL_ACTION_EXEC) {
      if (parse_argv(argv, &buttons[i].argv, &buttons[i].argc, err) < 0 || !buttons[i].argc)
        goto fail;
    } else if (argv) {
      goto fail;
    }
    if (buttons[i].target != RAWPANEL_TARGET_STATUS && buttons[i].status_id)
      goto fail;
  }
  cfg->buttons = buttons;
  cfg->button_count = (size_t)n;
  return 0;

fail:
  while (i >= 0) free_button(&buttons[i--]);
  free(buttons);
bad:
  if (err)
    fputs("rawpanel: invalid button configuration\n", err);
  return -1;
}

int rawpanel_config_load(RawpanelConfig *cfg, const char *path, FILE *err)
{
  config_t doc;
  config_setting_t *panel, *status, *sources, *systray, *buttons;
  const char *s = NULL;
  int value;

  if (!cfg || !path)
    return -1;
  config_init(&doc);
  if (!config_read_file(&doc, path)) {
    if (err)
      fprintf(err, "rawpanel: %s:%d: %s\n", config_error_file(&doc),
              config_error_line(&doc), config_error_text(&doc));
    config_destroy(&doc);
    return -1;
  }

  panel = config_lookup(&doc, "panel");
  if (!panel || config_setting_type(panel) != CONFIG_TYPE_GROUP)
    goto invalid;
  if (config_setting_lookup_string(panel, "font", &s) && replace_string(&cfg->font, s) < 0)
    goto oom;
  if (config_setting_lookup_string(panel, "position", &s)) {
    if (!strcmp(s, "top")) cfg->top = true;
    else if (!strcmp(s, "bottom")) cfg->top = false;
    else goto invalid;
  }
#define UINT_SETTING(name, field, minv, maxv) \
  do { \
    if (config_setting_lookup_int(panel, name, &value)) { \
      if (value < (minv) || value > (maxv)) \
        goto invalid; \
      cfg->field = (unsigned int)value; \
    } \
  } while (0)
  UINT_SETTING("height", height, 12, 256);
  UINT_SETTING("padding", padding, 0, 128);
  UINT_SETTING("item_spacing", item_spacing, 0, 128);
  UINT_SETTING("section_spacing", section_spacing, 0, 256);
  UINT_SETTING("cell_margin", cell_margin, 0, 128);
  UINT_SETTING("indicator_size", indicator_size, 0, 32);
#undef UINT_SETTING
  if (config_setting_lookup_string(panel, "tags", &s)) {
    if (!strcmp(s, "all")) cfg->tags_all = true;
    else if (!strcmp(s, "occupied")) cfg->tags_all = false;
    else goto invalid;
  }
  if (parse_color_group(cfg, config_setting_get_member(panel, "colors"), err) < 0)
    goto fail;

  systray = config_lookup(&doc, "systray");
  if (systray) {
    if (config_setting_type(systray) != CONFIG_TYPE_GROUP)
      goto invalid;
    if (config_setting_lookup_bool(systray, "enabled", &value))
      cfg->systray_enabled = value != 0;
    if (config_setting_lookup_int(systray, "spacing", &value)) {
      if (value < 0 || value > 128) goto invalid;
      cfg->systray_spacing = (unsigned int)value;
    }
    if (config_setting_lookup_int(systray, "icon_size", &value)) {
      if (value < 8 || value > 256) goto invalid;
      cfg->systray_icon_size = (unsigned int)value;
    }
  }

  status = config_lookup(&doc, "status");
  if (status) {
    if (config_setting_type(status) != CONFIG_TYPE_GROUP)
      goto invalid;
    if (config_setting_lookup_bool(status, "enabled", &value))
      cfg->status_enabled = value != 0;
    if (config_setting_lookup_string(status, "separator", &s) &&
        replace_string(&cfg->status_separator, s) < 0)
      goto oom;
    sources = config_setting_get_member(status, "sources");
    if (sources && rawpanel_status_config_parse_sources(&cfg->status, sources, err) < 0)
      goto fail;
    if (rawpanel_status_config_validate(&cfg->status, err) < 0)
      goto fail;
  }

  buttons = config_lookup(&doc, "buttons");
  if (parse_buttons(cfg, buttons, err) < 0)
    goto fail;

  config_destroy(&doc);
  return 0;
invalid:
  if (err) fputs("rawpanel: invalid configuration\n", err);
  goto fail;
oom:
  if (err) fputs("rawpanel: out of memory while loading configuration\n", err);
fail:
  config_destroy(&doc);
  return -1;
}

int rawpanel_config_find_default(char *buffer, size_t capacity)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  int n;
  if (!buffer || !capacity)
    return -1;
  if (xdg && *xdg)
    n = snprintf(buffer, capacity, "%s/rawpanel/rawpanel.conf", xdg);
  else if (home && *home)
    n = snprintf(buffer, capacity, "%s/.config/rawpanel/rawpanel.conf", home);
  else
    n = snprintf(buffer, capacity, "/etc/rawpanel/rawpanel.conf");
  return n < 0 || (size_t)n >= capacity ? -1 : 0;
}

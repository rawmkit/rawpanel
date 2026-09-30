/* SPDX-License-Identifier: MIT */
#include "config.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

static char *
dupstr(const char *s)
{
  size_t n;
  char *p;

  if (!s)
    return NULL;
  n = strlen(s) + 1u;
  p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

static int
cfg_error(FILE *err, const config_setting_t *setting, const char *fmt, ...)
{
  const char *file = setting ? config_setting_source_file(setting) : NULL;
  unsigned int line = setting ? config_setting_source_line(setting) : 0;
  va_list args;

  if (!err)
    return -1;
  fputs("rawpanel: configuration error", err);
  if (file && *file)
    fprintf(err, " in %s", file);
  if (line)
    fprintf(err, ":%u", line);
  fputs(": ", err);
  va_start(args, fmt);
  vfprintf(err, fmt, args);
  va_end(args);
  fputc('\n', err);
  return -1;
}

static int
name_allowed(const char *name, const char *const *allowed, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (!strcmp(name, allowed[i]))
      return 1;
  return 0;
}

static int
validate_members(FILE *err, const config_setting_t *group,
                 const char *const *allowed, size_t count)
{
  int i, n;

  if (!group || config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "expected a group");
  n = config_setting_length(group);
  for (i = 0; i < n; ++i)
  {
    config_setting_t *member = config_setting_get_elem(group, (unsigned int)i);
    const char *name = member ? config_setting_name(member) : NULL;

    if (!name || !name_allowed(name, allowed, count))
      return cfg_error(err, member, "unknown setting '%s'", name ? name : "");
  }
  return 0;
}

static int
replace_string(char **dst, const char *src)
{
  char *copy = dupstr(src);

  if (!copy)
    return -1;
  free(*dst);
  *dst = copy;
  return 0;
}

static int
read_string(FILE *err, const config_setting_t *group, const char *name,
            int allow_empty, char **out)
{
  config_setting_t *setting = config_setting_get_member(group, name);
  const char *value;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_STRING)
    return cfg_error(err, setting, "%s must be a string", name);
  value = config_setting_get_string(setting);
  if (!allow_empty && (!value || !*value))
    return cfg_error(err, setting, "%s must not be empty", name);
  if (replace_string(out, value ? value : "") < 0)
  {
    if (err)
      fputs("rawpanel: out of memory while loading configuration\n", err);
    return -1;
  }
  return 0;
}

static int
read_bool(FILE *err, const config_setting_t *group, const char *name,
          bool *out)
{
  config_setting_t *setting = config_setting_get_member(group, name);

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_BOOL)
    return cfg_error(err, setting, "%s must be a boolean", name);
  *out = config_setting_get_bool(setting) != 0;
  return 0;
}

static int
read_uint(FILE *err, const config_setting_t *group, const char *name,
          unsigned int min, unsigned int max, unsigned int *out)
{
  config_setting_t *setting = config_setting_get_member(group, name);
  long long value;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_INT &&
      config_setting_type(setting) != CONFIG_TYPE_INT64)
    return cfg_error(err, setting, "%s must be an integer", name);
  value = config_setting_type(setting) == CONFIG_TYPE_INT
            ? (long long)config_setting_get_int(setting)
            : config_setting_get_int64(setting);
  if (value < (long long)min || value > (long long)max)
    return cfg_error(err, setting, "%s is out of range", name);
  *out = (unsigned int)value;
  return 0;
}

static void
free_button(RawpanelButton *button)
{
  size_t i;

  if (!button)
    return;
  free(button->status_id);
  free(button->command);
  free(button->layout);
  for (i = 0; i < button->argc; ++i)
    free(button->argv[i]);
  free(button->argv);
  memset(button, 0, sizeof(*button));
}

static void
clear_buttons(RawpanelConfig *cfg)
{
  size_t i;

  for (i = 0; i < cfg->button_count; ++i)
    free_button(&cfg->buttons[i]);
  free(cfg->buttons);
  cfg->buttons = NULL;
  cfg->button_count = 0;
}

void
rawpanel_config_init(RawpanelConfig *cfg)
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

void
rawpanel_config_destroy(RawpanelConfig *cfg)
{
  size_t i, j;

  if (!cfg)
    return;
  free(cfg->font);
  free(cfg->status_separator);
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
      free(cfg->colors[i][j]);
  clear_buttons(cfg);
  rawpanel_status_config_destroy(&cfg->status);
  memset(cfg, 0, sizeof(*cfg));
}

static int
parse_color_group(RawpanelConfig *cfg, const config_setting_t *colors,
                  FILE *err)
{
  static const char *const schemes[] = {
    "normal", "selected", "floating", "urgent", "error"
  };
  static const char *const components[] = {
    "border", "foreground", "background"
  };
  size_t i, j;

  if (!colors)
    return 0;
  if (config_setting_type(colors) != CONFIG_TYPE_GROUP)
    return cfg_error(err, colors, "panel.colors must be a group");
  if (validate_members(err, colors, schemes,
                       sizeof(schemes) / sizeof(schemes[0])) < 0)
    return -1;

  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
  {
    const config_setting_t *scheme =
      config_setting_get_member(colors, schemes[i]);

    if (!scheme)
      continue;
    if (config_setting_type(scheme) != CONFIG_TYPE_GROUP)
      return cfg_error(err, scheme, "panel.colors.%s must be a group", schemes[i]);
    if (validate_members(err, scheme, components,
                         sizeof(components) / sizeof(components[0])) < 0)
      return -1;
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
    {
      config_setting_t *setting = config_setting_get_member(scheme, components[j]);
      const char *value;

      if (!setting)
        continue;
      if (config_setting_type(setting) != CONFIG_TYPE_STRING)
        return cfg_error(err, setting, "%s must be a string", components[j]);
      value = config_setting_get_string(setting);
      if (!value || !*value)
        return cfg_error(err, setting, "%s must not be empty", components[j]);
      if (replace_string(&cfg->colors[i][j], value) < 0)
        return -1;
    }
  }
  return 0;
}

static unsigned int
parse_modifiers(const config_setting_t *setting, FILE *err)
{
  unsigned int result = 0;
  int i, n;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
      config_setting_type(setting) != CONFIG_TYPE_ARRAY)
  {
    cfg_error(err, setting, "button modifiers must be a list");
    return UINT_MAX;
  }
  n = config_setting_length(setting);
  for (i = 0; i < n; ++i)
  {
    config_setting_t *item = config_setting_get_elem(setting, (unsigned int)i);
    const char *name;

    if (!item || config_setting_type(item) != CONFIG_TYPE_STRING)
    {
      cfg_error(err, item, "button modifiers must be strings");
      return UINT_MAX;
    }
    name = config_setting_get_string(item);
    if (!strcmp(name, "Shift")) result |= 1u << 0;
    else if (!strcmp(name, "Control")) result |= 1u << 1;
    else if (!strcmp(name, "Mod1")) result |= 1u << 2;
    else if (!strcmp(name, "Mod4")) result |= 1u << 3;
    else
    {
      cfg_error(err, item, "unknown button modifier '%s'", name);
      return UINT_MAX;
    }
  }
  return result;
}

static int
parse_target(const char *text, RawpanelTarget *target)
{
  if (!strcmp(text, "tag_bar")) *target = RAWPANEL_TARGET_TAGS;
  else if (!strcmp(text, "layout")) *target = RAWPANEL_TARGET_LAYOUT;
  else if (!strcmp(text, "title")) *target = RAWPANEL_TARGET_TITLE;
  else if (!strcmp(text, "status")) *target = RAWPANEL_TARGET_STATUS;
  else return -1;
  return 0;
}

static int
parse_action(const char *text, RawpanelActionType *type, char **command)
{
  if (!strcmp(text, "volume_up")) *type = RAWPANEL_ACTION_VOLUME_UP;
  else if (!strcmp(text, "volume_down")) *type = RAWPANEL_ACTION_VOLUME_DOWN;
  else if (!strcmp(text, "volume_mute")) *type = RAWPANEL_ACTION_VOLUME_MUTE;
  else if (!strcmp(text, "exec")) *type = RAWPANEL_ACTION_EXEC;
  else
  {
    *type = RAWPANEL_ACTION_RAWM;
    *command = dupstr(text);
    if (!*command)
      return -1;
  }
  return 0;
}

static int
parse_argv(const config_setting_t *setting, char ***argv, size_t *argc,
           FILE *err)
{
  int i, n;
  char **out;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
      config_setting_type(setting) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, setting, "argv must be a list");
  n = config_setting_length(setting);
  if (n <= 0 || n > RAWPANEL_MAX_ARGV)
    return cfg_error(err, setting, "argv length is out of range");
  out = calloc((size_t)n + 1u, sizeof(*out));
  if (!out)
    return -1;
  for (i = 0; i < n; ++i)
  {
    config_setting_t *arg = config_setting_get_elem(setting, (unsigned int)i);
    const char *text;

    if (!arg || config_setting_type(arg) != CONFIG_TYPE_STRING)
    {
      cfg_error(err, arg, "argv entries must be strings");
      goto fail;
    }
    text = config_setting_get_string(arg);
    if (!text || !*text || !(out[i] = dupstr(text)))
    {
      cfg_error(err, arg, "argv entries must not be empty");
      goto fail;
    }
  }
  *argv = out;
  *argc = (size_t)n;
  return 0;

fail:
  while (i-- > 0)
    free(out[i]);
  free(out);
  return -1;
}

static int
parse_optional_string(FILE *err, const config_setting_t *entry,
                      const char *name, char **out)
{
  config_setting_t *setting = config_setting_get_member(entry, name);
  const char *text;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_STRING)
    return cfg_error(err, setting, "%s must be a string", name);
  text = config_setting_get_string(setting);
  if (!text || !*text)
    return cfg_error(err, setting, "%s must not be empty", name);
  *out = dupstr(text);
  return *out ? 0 : -1;
}

static int
parse_buttons(RawpanelConfig *cfg, const config_setting_t *list, FILE *err)
{
  static const char *const allowed[] = {
    "target", "modifiers", "button", "action", "id", "number", "tag",
    "layout", "argv"
  };
  int i, n;
  RawpanelButton *buttons;

  if (!list)
    return 0;
  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, list, "buttons must be a list");
  n = config_setting_length(list);
  if (n < 0 || n > RAWPANEL_MAX_BUTTONS)
    return cfg_error(err, list, "button count is out of range");
  buttons = calloc((size_t)n, sizeof(*buttons));
  if (n && !buttons)
    return -1;

  for (i = 0; i < n; ++i)
  {
    config_setting_t *item = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *setting;
    const char *target, *action;
    unsigned int parsed_mods;
    int value;

    if (!item || config_setting_type(item) != CONFIG_TYPE_GROUP)
    {
      cfg_error(err, item, "button entries must be groups");
      goto fail;
    }
    if (validate_members(err, item, allowed,
                         sizeof(allowed) / sizeof(allowed[0])) < 0)
      goto fail;

    setting = config_setting_get_member(item, "target");
    if (!setting || config_setting_type(setting) != CONFIG_TYPE_STRING)
    {
      cfg_error(err, item, "button requires string target");
      goto fail;
    }
    target = config_setting_get_string(setting);
    if (parse_target(target, &buttons[i].target) < 0)
    {
      cfg_error(err, setting, "unknown button target '%s'", target);
      goto fail;
    }

    setting = config_setting_get_member(item, "button");
    if (!setting || config_setting_type(setting) != CONFIG_TYPE_INT)
    {
      cfg_error(err, item, "button requires integer button number");
      goto fail;
    }
    value = config_setting_get_int(setting);
    if (value < 1 || value > 5)
    {
      cfg_error(err, setting, "button number is out of range");
      goto fail;
    }
    buttons[i].button = (unsigned int)value;

    setting = config_setting_get_member(item, "action");
    if (!setting || config_setting_type(setting) != CONFIG_TYPE_STRING)
    {
      cfg_error(err, item, "button requires string action");
      goto fail;
    }
    action = config_setting_get_string(setting);
    if (!action || !*action ||
        parse_action(action, &buttons[i].type, &buttons[i].command) < 0)
    {
      cfg_error(err, setting, "invalid button action");
      goto fail;
    }

    parsed_mods = parse_modifiers(config_setting_get_member(item, "modifiers"), err);
    if (parsed_mods == UINT_MAX)
      goto fail;
    buttons[i].modifiers = parsed_mods;

    if (parse_optional_string(err, item, "id", &buttons[i].status_id) < 0 ||
        parse_optional_string(err, item, "layout", &buttons[i].layout) < 0)
      goto fail;

    setting = config_setting_get_member(item, "number");
    if (setting)
    {
      if (config_setting_type(setting) != CONFIG_TYPE_INT)
      {
        cfg_error(err, setting, "number must be an integer");
        goto fail;
      }
      buttons[i].has_number = true;
      buttons[i].number = config_setting_get_int(setting);
    }

    setting = config_setting_get_member(item, "tag");
    if (setting)
    {
      if (config_setting_type(setting) != CONFIG_TYPE_INT)
      {
        cfg_error(err, setting, "tag must be an integer");
        goto fail;
      }
      value = config_setting_get_int(setting);
      if (value < 1 || value > 32)
      {
        cfg_error(err, setting, "tag is out of range");
        goto fail;
      }
      buttons[i].has_tag = true;
      buttons[i].tag = (unsigned int)value;
    }

    setting = config_setting_get_member(item, "argv");
    if (buttons[i].type == RAWPANEL_ACTION_EXEC)
    {
      if (!setting || parse_argv(setting, &buttons[i].argv,
                                 &buttons[i].argc, err) < 0)
        goto fail;
    }
    else if (setting)
    {
      cfg_error(err, setting, "argv is valid only for exec actions");
      goto fail;
    }
    if (buttons[i].target != RAWPANEL_TARGET_STATUS && buttons[i].status_id)
    {
      cfg_error(err, item, "id is valid only for status buttons");
      goto fail;
    }
  }

  clear_buttons(cfg);
  cfg->buttons = buttons;
  cfg->button_count = (size_t)n;
  return 0;

fail:
  while (i >= 0)
    free_button(&buttons[i--]);
  free(buttons);
  return -1;
}

static int
apply_config(RawpanelConfig *cfg, const char *path, FILE *err)
{
  static const char *const root_allowed[] = {
    "panel", "systray", "status", "buttons"
  };
  static const char *const panel_allowed[] = {
    "font", "position", "height", "padding", "item_spacing",
    "section_spacing", "cell_margin", "indicator_size", "tags", "colors"
  };
  static const char *const systray_allowed[] = {
    "enabled", "spacing", "icon_size"
  };
  static const char *const status_allowed[] = {
    "enabled", "separator", "sources"
  };
  config_t doc;
  config_setting_t *root, *panel, *systray, *status, *setting;
  const char *text;

  config_init(&doc);
  if (!config_read_file(&doc, path))
  {
    if (err)
      fprintf(err, "rawpanel: %s:%d: %s\n",
              config_error_file(&doc) ? config_error_file(&doc) : path,
              config_error_line(&doc), config_error_text(&doc));
    config_destroy(&doc);
    return -1;
  }

  root = config_root_setting(&doc);
  if (validate_members(err, root, root_allowed,
                       sizeof(root_allowed) / sizeof(root_allowed[0])) < 0)
    goto fail;

  panel = config_setting_get_member(root, "panel");
  if (panel)
  {
    if (validate_members(err, panel, panel_allowed,
                         sizeof(panel_allowed) / sizeof(panel_allowed[0])) < 0)
      goto fail;
    if (read_string(err, panel, "font", 0, &cfg->font) < 0 ||
        read_uint(err, panel, "height", 12, 256, &cfg->height) < 0 ||
        read_uint(err, panel, "padding", 0, 128, &cfg->padding) < 0 ||
        read_uint(err, panel, "item_spacing", 0, 128,
                  &cfg->item_spacing) < 0 ||
        read_uint(err, panel, "section_spacing", 0, 256,
                  &cfg->section_spacing) < 0 ||
        read_uint(err, panel, "cell_margin", 0, 128,
                  &cfg->cell_margin) < 0 ||
        read_uint(err, panel, "indicator_size", 0, 32,
                  &cfg->indicator_size) < 0 ||
        parse_color_group(cfg, config_setting_get_member(panel, "colors"), err) < 0)
      goto fail;

    setting = config_setting_get_member(panel, "position");
    if (setting)
    {
      if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      {
        cfg_error(err, setting, "position must be a string");
        goto fail;
      }
      text = config_setting_get_string(setting);
      if (!strcmp(text, "top")) cfg->top = true;
      else if (!strcmp(text, "bottom")) cfg->top = false;
      else
      {
        cfg_error(err, setting, "position must be 'top' or 'bottom'");
        goto fail;
      }
    }

    setting = config_setting_get_member(panel, "tags");
    if (setting)
    {
      if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      {
        cfg_error(err, setting, "tags must be a string");
        goto fail;
      }
      text = config_setting_get_string(setting);
      if (!strcmp(text, "all")) cfg->tags_all = true;
      else if (!strcmp(text, "occupied")) cfg->tags_all = false;
      else
      {
        cfg_error(err, setting, "tags must be 'all' or 'occupied'");
        goto fail;
      }
    }
  }

  systray = config_setting_get_member(root, "systray");
  if (systray)
  {
    if (validate_members(err, systray, systray_allowed,
                         sizeof(systray_allowed) / sizeof(systray_allowed[0])) < 0 ||
        read_bool(err, systray, "enabled", &cfg->systray_enabled) < 0 ||
        read_uint(err, systray, "spacing", 0, 128,
                  &cfg->systray_spacing) < 0 ||
        read_uint(err, systray, "icon_size", 8, 256,
                  &cfg->systray_icon_size) < 0)
      goto fail;
  }

  status = config_setting_get_member(root, "status");
  if (status)
  {
    setting = config_setting_get_member(status, "sources");
    if (validate_members(err, status, status_allowed,
                         sizeof(status_allowed) / sizeof(status_allowed[0])) < 0 ||
        read_bool(err, status, "enabled", &cfg->status_enabled) < 0 ||
        read_string(err, status, "separator", 1, &cfg->status_separator) < 0 ||
        (setting && rawpanel_status_config_parse_sources(&cfg->status,
                                                         setting, err) < 0))
      goto fail;
  }

  if (parse_buttons(cfg, config_setting_get_member(root, "buttons"), err) < 0 ||
      rawpanel_status_config_validate(&cfg->status, err) < 0)
    goto fail;

  config_destroy(&doc);
  return 0;

fail:
  config_destroy(&doc);
  return -1;
}

static int
file_readable(const char *path)
{
  return path && access(path, R_OK) == 0;
}

static int
user_config_path(char *buffer, size_t capacity)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  int length;

  if (xdg && xdg[0] == '/')
    length = snprintf(buffer, capacity, "%s/rawpanel/rawpanel.conf", xdg);
  else if (home && home[0] == '/')
    length = snprintf(buffer, capacity, "%s/.config/rawpanel/rawpanel.conf", home);
  else
    return 0;
  if (length < 0 || (size_t)length >= capacity)
    return -1;
  return 1;
}

int
rawpanel_config_load(RawpanelConfig *cfg, const char *explicit_path, FILE *err)
{
  char system_path[PATH_MAX];
  char user_path[PATH_MAX];
  int length, user_result;

  if (!cfg)
    return -1;
  if (explicit_path)
    return apply_config(cfg, explicit_path, err);

  length = snprintf(system_path, sizeof(system_path),
                    "%s/rawpanel/rawpanel.conf", SYSCONFDIR);
  if (length < 0 || (size_t)length >= sizeof(system_path))
    return -1;
  if (file_readable(system_path) && apply_config(cfg, system_path, err) < 0)
    return -1;

  user_result = user_config_path(user_path, sizeof(user_path));
  if (user_result < 0)
    return -1;
  if (user_result > 0 && file_readable(user_path) &&
      apply_config(cfg, user_path, err) < 0)
    return -1;
  return 0;
}

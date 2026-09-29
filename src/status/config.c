/* SPDX-License-Identifier: MIT */
#include "config.h"
#include "status.h"

#include <limits.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void *
xcalloc(size_t n, size_t size)
{
  void *p = calloc(n ? n : 1, size ? size : 1);

  if (!p) {
    fputs("rawpanel: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static char *
xstrdup(const char *s)
{
  char *p = strdup(s ? s : "");

  if (!p) {
    fputs("rawpanel: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static void
free_argv(char **argv, size_t argc)
{
  size_t i;

  for (i = 0; i < argc; ++i)
    free(argv[i]);
  free(argv);
}

static void
free_source(RawpanelStatusSourceConfig *source)
{
  if (!source)
    return;
  free(source->id);
  free(source->device);
  free(source->control);
  free(source->format);
  free(source->path);
  free(source->interface);
  free_argv(source->argv, source->argc);
  memset(source, 0, sizeof(*source));
}

void
rawpanel_status_config_init(RawpanelStatusConfig *cfg)
{
  RawpanelStatusSourceConfig *source;

  memset(cfg, 0, sizeof(*cfg));
  cfg->sources = xcalloc(1, sizeof(*cfg->sources));
  cfg->source_count = 1;
  source = &cfg->sources[0];
  source->id = xstrdup("clock");
  source->enabled = true;
  source->provider = RAWPANEL_STATUS_PROVIDER_CLOCK;
  source->period_ms = 5000;
  source->format = xstrdup("%a %b %d %H:%M");
  source->timeout_ms = RAWPANEL_STATUS_DEFAULT_EXEC_TIMEOUT_MS;
}

void
rawpanel_status_config_destroy(RawpanelStatusConfig *cfg)
{
  size_t i;

  for (i = 0; i < cfg->source_count; ++i)
    free_source(&cfg->sources[i]);
  free(cfg->sources);
  memset(cfg, 0, sizeof(*cfg));
}

const char *
rawpanel_status_provider_name(RawpanelStatusProvider provider)
{
  switch (provider) {
  case RAWPANEL_STATUS_PROVIDER_ALSA: return "alsa";
  case RAWPANEL_STATUS_PROVIDER_BATTERY: return "battery";
  case RAWPANEL_STATUS_PROVIDER_CLOCK: return "clock";
  case RAWPANEL_STATUS_PROVIDER_CPU: return "cpu";
  case RAWPANEL_STATUS_PROVIDER_EXEC: return "exec";
  case RAWPANEL_STATUS_PROVIDER_FILESYSTEM: return "filesystem";
  case RAWPANEL_STATUS_PROVIDER_MEMORY: return "memory";
  case RAWPANEL_STATUS_PROVIDER_NETWORK: return "network";
  }
  return NULL;
}

static int
parse_provider(const char *name, RawpanelStatusProvider *provider)
{
  int i;

  for (i = RAWPANEL_STATUS_PROVIDER_ALSA;
       i <= RAWPANEL_STATUS_PROVIDER_NETWORK; ++i) {
    RawpanelStatusProvider candidate = (RawpanelStatusProvider)i;
    const char *candidate_name = rawpanel_status_provider_name(candidate);

    if (candidate_name && !strcmp(name, candidate_name)) {
      *provider = candidate;
      return 0;
    }
  }
  return -1;
}

static int
cfg_error(FILE *err, const config_setting_t *setting, const char *fmt, ...)
{
  const char *file = setting ? config_setting_source_file(setting) : NULL;
  unsigned int line = setting ? config_setting_source_line(setting) : 0;
  va_list args;

  if (file && line)
    fprintf(err, "rawpanel: %s:%u: ", file, line);
  else
    fputs("rawpanel: configuration error: ", err);
  va_start(args, fmt);
  vfprintf(err, fmt, args);
  va_end(args);
  fputc('\n', err);
  return -1;
}

static bool
name_allowed(const char *name, const char *const *allowed, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (!strcmp(name, allowed[i]))
      return true;
  return false;
}

static int
validate_members(FILE *err, const config_setting_t *group,
                 const char *const *allowed, size_t count)
{
  int i, n;

  if (!group || config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "expected a group%s", "");
  n = config_setting_length(group);
  for (i = 0; i < n; ++i) {
    config_setting_t *member = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(member);

    if (!name_allowed(name, allowed, count))
      return cfg_error(err, member, "unknown setting '%s'", name);
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

static int
replace_optional_string(FILE *err, const config_setting_t *group,
                        const char *name, char **out)
{
  config_setting_t *setting = config_setting_get_member(group, name);

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_STRING)
    return cfg_error(err, setting, "%s must be a string", name);
  free(*out);
  *out = xstrdup(config_setting_get_string(setting));
  return 0;
}

static int
replace_command(FILE *err, const config_setting_t *group,
                RawpanelStatusSourceConfig *source)
{
  config_setting_t *setting = config_setting_get_member(group, "command");
  char **argv;
  int i, n;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_ARRAY &&
      config_setting_type(setting) != CONFIG_TYPE_LIST)
    return cfg_error(err, setting, "command must be a list%s", "");
  n = config_setting_length(setting);
  if (n <= 0)
    return cfg_error(err, setting, "command must not be empty%s", "");
  argv = xcalloc((size_t)n + 1, sizeof(*argv));
  for (i = 0; i < n; ++i) {
    config_setting_t *arg = config_setting_get_elem(setting, (unsigned int)i);

    if (config_setting_type(arg) != CONFIG_TYPE_STRING) {
      free_argv(argv, (size_t)i);
      return cfg_error(err, arg, "command arguments must be strings%s", "");
    }
    argv[i] = xstrdup(config_setting_get_string(arg));
  }
  free_argv(source->argv, source->argc);
  source->argv = argv;
  source->argc = (size_t)n;
  return 0;
}

static void
clear_provider_fields(RawpanelStatusSourceConfig *source)
{
  free(source->device);
  source->device = NULL;
  free(source->control);
  source->control = NULL;
  free(source->format);
  source->format = NULL;
  free(source->path);
  source->path = NULL;
  free(source->interface);
  source->interface = NULL;
  free_argv(source->argv, source->argc);
  source->argv = NULL;
  source->argc = 0;
}

static void
set_provider_defaults(RawpanelStatusSourceConfig *source,
                      RawpanelStatusProvider provider)
{
  clear_provider_fields(source);
  source->provider = provider;
  source->enabled = true;
  source->period_ms = 5000;
  source->warning = 0;
  source->critical = 0;
  source->step = 0;
  source->timeout_ms = RAWPANEL_STATUS_DEFAULT_EXEC_TIMEOUT_MS;

  switch (provider) {
  case RAWPANEL_STATUS_PROVIDER_ALSA:
    source->period_ms = 5000;
    source->step = 1;
    source->device = xstrdup("default");
    source->control = xstrdup("Master");
    break;
  case RAWPANEL_STATUS_PROVIDER_BATTERY:
    source->period_ms = 15000;
    source->warning = 20;
    source->critical = 10;
    break;
  case RAWPANEL_STATUS_PROVIDER_CLOCK:
    source->format = xstrdup("%a %b %d %H:%M");
    break;
  case RAWPANEL_STATUS_PROVIDER_CPU:
    source->period_ms = 2000;
    source->warning = 80;
    source->critical = 95;
    break;
  case RAWPANEL_STATUS_PROVIDER_FILESYSTEM:
    source->period_ms = 60000;
    source->warning = 20;
    source->critical = 5;
    source->path = xstrdup("/");
    break;
  case RAWPANEL_STATUS_PROVIDER_MEMORY:
    source->warning = 85;
    source->critical = 95;
    break;
  case RAWPANEL_STATUS_PROVIDER_NETWORK:
    source->period_ms = 2000;
    source->interface = xstrdup("default");
    break;
  case RAWPANEL_STATUS_PROVIDER_EXEC:
    break;
  }
}

static RawpanelStatusSourceConfig *
find_source(RawpanelStatusConfig *cfg, const char *id)
{
  size_t i;

  for (i = 0; i < cfg->source_count; ++i)
    if (!strcmp(cfg->sources[i].id, id))
      return &cfg->sources[i];
  return NULL;
}

static RawpanelStatusSourceConfig *
append_source(RawpanelStatusConfig *cfg, const char *id)
{
  RawpanelStatusSourceConfig *sources;
  RawpanelStatusSourceConfig *source;

  sources = realloc(cfg->sources,
                    (cfg->source_count + 1) * sizeof(*cfg->sources));
  if (!sources) {
    fputs("rawpanel: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  cfg->sources = sources;
  source = &cfg->sources[cfg->source_count++];
  memset(source, 0, sizeof(*source));
  source->id = xstrdup(id);
  source->enabled = true;
  return source;
}

static int
parse_source(FILE *err, RawpanelStatusSourceConfig *source,
             const config_setting_t *group, bool existing)
{
  static const char *const allowed[] = {
    "enabled", "provider", "period_ms", "warning", "critical", "step",
    "device", "control", "format", "path", "interface", "command",
    "timeout_ms"
  };
  config_setting_t *setting;
  const char *provider_name;
  RawpanelStatusProvider provider;

  if (validate_members(err, group, allowed,
                       sizeof(allowed) / sizeof(allowed[0])) < 0)
    return -1;

  setting = config_setting_get_member(group, "provider");
  if (setting) {
    if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      return cfg_error(err, setting, "source provider must be a string%s", "");
    provider_name = config_setting_get_string(setting);
    if (parse_provider(provider_name, &provider) < 0)
      return cfg_error(err, setting, "unknown provider '%s'", provider_name);
    if (!existing || provider != source->provider)
      set_provider_defaults(source, provider);
  } else if (!existing) {
    return cfg_error(err, group, "new source requires a provider%s", "");
  }

  if (source->provider != RAWPANEL_STATUS_PROVIDER_ALSA &&
      config_setting_get_member(group, "step"))
    return cfg_error(err, config_setting_get_member(group, "step"),
                     "step is valid only for alsa provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_ALSA &&
      config_setting_get_member(group, "device"))
    return cfg_error(err, config_setting_get_member(group, "device"),
                     "device is valid only for alsa provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_ALSA &&
      config_setting_get_member(group, "control"))
    return cfg_error(err, config_setting_get_member(group, "control"),
                     "control is valid only for alsa provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_CLOCK &&
      config_setting_get_member(group, "format"))
    return cfg_error(err, config_setting_get_member(group, "format"),
                     "format is valid only for clock provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_FILESYSTEM &&
      config_setting_get_member(group, "path"))
    return cfg_error(err, config_setting_get_member(group, "path"),
                     "path is valid only for filesystem provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_NETWORK &&
      config_setting_get_member(group, "interface"))
    return cfg_error(err, config_setting_get_member(group, "interface"),
                     "interface is valid only for network provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_EXEC &&
      config_setting_get_member(group, "command"))
    return cfg_error(err, config_setting_get_member(group, "command"),
                     "command is valid only for exec provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_EXEC &&
      config_setting_get_member(group, "timeout_ms"))
    return cfg_error(err, config_setting_get_member(group, "timeout_ms"),
                     "timeout_ms is valid only for exec provider%s", "");
  if (source->provider != RAWPANEL_STATUS_PROVIDER_BATTERY &&
      source->provider != RAWPANEL_STATUS_PROVIDER_CPU &&
      source->provider != RAWPANEL_STATUS_PROVIDER_FILESYSTEM &&
      source->provider != RAWPANEL_STATUS_PROVIDER_MEMORY &&
      (config_setting_get_member(group, "warning") ||
       config_setting_get_member(group, "critical")))
    return cfg_error(err,
                     config_setting_get_member(group, "warning")
                       ? config_setting_get_member(group, "warning")
                       : config_setting_get_member(group, "critical"),
                     "warning/critical are not valid for this provider%s", "");

  if (read_bool(err, group, "enabled", &source->enabled) < 0 ||
      read_uint(err, group, "period_ms", 100, RAWPANEL_STATUS_MAX_PERIOD_MS,
                &source->period_ms) < 0 ||
      read_uint(err, group, "warning", 0, UINT_MAX, &source->warning) < 0 ||
      read_uint(err, group, "critical", 0, UINT_MAX, &source->critical) < 0 ||
      read_uint(err, group, "step", 1, 100, &source->step) < 0 ||
      read_uint(err, group, "timeout_ms", 1, 600000,
                &source->timeout_ms) < 0 ||
      replace_optional_string(err, group, "device", &source->device) < 0 ||
      replace_optional_string(err, group, "control", &source->control) < 0 ||
      replace_optional_string(err, group, "format", &source->format) < 0 ||
      replace_optional_string(err, group, "path", &source->path) < 0 ||
      replace_optional_string(err, group, "interface", &source->interface) < 0 ||
      replace_command(err, group, source) < 0)
    return -1;
  return 0;
}

int
rawpanel_status_config_parse_sources(RawpanelStatusConfig *cfg,
                                 const config_setting_t *group, FILE *err)
{
  int i, n;

  if (!group || config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "status.sources must be a group%s", "");
  n = config_setting_length(group);
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(entry);
    RawpanelStatusSourceConfig *source;
    bool existing;

    if (!name || !rawpanel_status_id_valid((const unsigned char *)name,
                                       strlen(name)))
      return cfg_error(err, entry, "invalid source id '%s'", name ? name : "");
    source = find_source(cfg, name);
    existing = source != NULL;
    if (!source)
      source = append_source(cfg, name);
    if (parse_source(err, source, entry, existing) < 0)
      return -1;
    if (existing && source != &cfg->sources[cfg->source_count - 1]) {
      RawpanelStatusSourceConfig moved = *source;
      size_t index = (size_t)(source - cfg->sources);

      memmove(&cfg->sources[index], &cfg->sources[index + 1],
              (cfg->source_count - index - 1) * sizeof(*cfg->sources));
      cfg->sources[cfg->source_count - 1] = moved;
    }
  }
  return 0;
}

int
rawpanel_status_config_validate(const RawpanelStatusConfig *cfg, FILE *err)
{
  size_t i;

  for (i = 0; i < cfg->source_count; ++i) {
    const RawpanelStatusSourceConfig *source = &cfg->sources[i];

    if (!rawpanel_status_id_valid((const unsigned char *)source->id,
                              strlen(source->id))) {
      fprintf(err, "rawpanel: configuration error: invalid status source id '%s'\n",
              source->id ? source->id : "");
      return -1;
    }
    if (!rawpanel_status_provider_name(source->provider)) {
      fprintf(err,
              "rawpanel: configuration error: status source '%s' has invalid provider\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWPANEL_STATUS_PROVIDER_ALSA &&
        (!source->device || !source->device[0] ||
         !source->control || !source->control[0])) {
      fprintf(err,
              "rawpanel: configuration error: alsa status source '%s' requires non-empty device and control\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWPANEL_STATUS_PROVIDER_EXEC && !source->argc) {
      fprintf(err,
              "rawpanel: configuration error: exec status source '%s' has no command\n",
              source->id);
      return -1;
    }
    if (source->provider != RAWPANEL_STATUS_PROVIDER_EXEC && source->argc) {
      fprintf(err,
              "rawpanel: configuration error: status source '%s' command is valid only for exec provider\n",
              source->id);
      return -1;
    }
    if ((source->provider == RAWPANEL_STATUS_PROVIDER_CPU ||
         source->provider == RAWPANEL_STATUS_PROVIDER_MEMORY) &&
        source->warning >= source->critical) {
      fprintf(err,
              "rawpanel: configuration error: status source '%s' requires warning < critical\n",
              source->id);
      return -1;
    }
    if ((source->provider == RAWPANEL_STATUS_PROVIDER_BATTERY ||
         source->provider == RAWPANEL_STATUS_PROVIDER_FILESYSTEM) &&
        source->critical >= source->warning) {
      fprintf(err,
              "rawpanel: configuration error: status source '%s' requires critical < warning\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWPANEL_STATUS_PROVIDER_CLOCK &&
        (!source->format || !source->format[0])) {
      fprintf(err,
              "rawpanel: configuration error: clock status source '%s' has empty format\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWPANEL_STATUS_PROVIDER_FILESYSTEM &&
        (!source->path || !source->path[0])) {
      fprintf(err,
              "rawpanel: configuration error: filesystem status source '%s' has empty path\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWPANEL_STATUS_PROVIDER_NETWORK &&
        (!source->interface || !source->interface[0] ||
         strlen(source->interface) >= IF_NAMESIZE ||
         strchr(source->interface, '/') || !strcmp(source->interface, ".") ||
         !strcmp(source->interface, ".."))) {
      fprintf(err,
              "rawpanel: configuration error: network status source '%s' has invalid interface\n",
              source->id);
      return -1;
    }
  }
  return 0;
}

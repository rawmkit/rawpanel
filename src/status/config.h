/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_STATUS_CONFIG_H
#define RAWPANEL_STATUS_CONFIG_H

#include <libconfig.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RAWPANEL_STATUS_DEFAULT_EXEC_TIMEOUT_MS 2000u
#define RAWPANEL_STATUS_MAX_PERIOD_MS 3600000u

typedef enum {
  RAWPANEL_STATUS_PROVIDER_ALSA,
  RAWPANEL_STATUS_PROVIDER_BATTERY,
  RAWPANEL_STATUS_PROVIDER_CLOCK,
  RAWPANEL_STATUS_PROVIDER_CPU,
  RAWPANEL_STATUS_PROVIDER_EXEC,
  RAWPANEL_STATUS_PROVIDER_FILESYSTEM,
  RAWPANEL_STATUS_PROVIDER_MEMORY,
  RAWPANEL_STATUS_PROVIDER_NETWORK
} RawpanelStatusProvider;

typedef struct {
  char *id;
  bool enabled;
  RawpanelStatusProvider provider;
  unsigned int period_ms;
  unsigned int warning;
  unsigned int critical;
  unsigned int step;
  char *device;
  char *control;
  char *format;
  char *path;
  char *interface;
  char **argv;
  size_t argc;
  unsigned int timeout_ms;
} RawpanelStatusSourceConfig;

typedef struct {
  RawpanelStatusSourceConfig *sources;
  size_t source_count;
} RawpanelStatusConfig;

void rawpanel_status_config_init(RawpanelStatusConfig *cfg);
void rawpanel_status_config_destroy(RawpanelStatusConfig *cfg);
int rawpanel_status_config_parse_sources(RawpanelStatusConfig *cfg,
                                     const config_setting_t *group, FILE *err);
int rawpanel_status_config_validate(const RawpanelStatusConfig *cfg, FILE *err);
const char *rawpanel_status_provider_name(RawpanelStatusProvider provider);

#endif

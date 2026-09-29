/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_STATUS_SOURCE_H
#define RAWPANEL_STATUS_SOURCE_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const RawpanelStatusSourceConfig *config;
  char *text;
  RawpanelStatusState state;
  bool has_value;
  bool cpu_baseline;
  uint64_t cpu_total;
  uint64_t cpu_idle;
  bool network_baseline;
  char *network_interface;
  uint64_t network_rx;
  uint64_t network_tx;
  uint64_t network_time_ms;
  void *provider_data;
  uint64_t next_due_ms;
} RawpanelStatusSource;

void rawpanel_status_source_init(RawpanelStatusSource *source,
                             const RawpanelStatusSourceConfig *config);
void rawpanel_status_source_destroy(RawpanelStatusSource *source);
int rawpanel_status_source_refresh(RawpanelStatusSource *source, uint64_t now_ms,
                               const volatile sig_atomic_t *terminate, FILE *err);
int rawpanel_status_source_control(RawpanelStatusSource *source,
                               RawpanelStatusControl control, uint64_t now_ms,
                               const volatile sig_atomic_t *terminate, FILE *err);
size_t rawpanel_status_source_pollfd_count(RawpanelStatusSource *source);
int rawpanel_status_source_pollfds(RawpanelStatusSource *source, struct pollfd *pfds,
                               size_t count);
int rawpanel_status_source_handle_events(RawpanelStatusSource *source,
                                     struct pollfd *pfds, size_t count,
                                     uint64_t now_ms,
                                     const volatile sig_atomic_t *terminate,
                                     FILE *err);

#endif

/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_STATUS_ALSA_H
#define RAWPANEL_STATUS_ALSA_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <poll.h>
#include <stddef.h>
#include <stdio.h>

typedef struct RawpanelStatusAlsa RawpanelStatusAlsa;

int rawpanel_status_alsa_sample(RawpanelStatusAlsa **state,
                            const RawpanelStatusSourceConfig *config,
                            char *buffer, size_t size,
                            RawpanelStatusState *semantic, FILE *err);
int rawpanel_status_alsa_control(RawpanelStatusAlsa **state,
                             const RawpanelStatusSourceConfig *config,
                             RawpanelStatusControl control, FILE *err);
void rawpanel_status_alsa_destroy(RawpanelStatusAlsa **state);
size_t rawpanel_status_alsa_pollfd_count(RawpanelStatusAlsa *state);
int rawpanel_status_alsa_pollfds(RawpanelStatusAlsa *state, struct pollfd *pfds,
                             size_t count);
int rawpanel_status_alsa_handle_events(RawpanelStatusAlsa **state,
                                   struct pollfd *pfds, size_t count,
                                   FILE *err);

#endif

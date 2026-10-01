/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_STATUS_ALSA_H
#define RAWPANEL_STATUS_ALSA_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <stddef.h>
#include <stdio.h>

int rawpanel_status_alsa_sample(const RawpanelStatusSourceConfig *config,
                                char *buffer, size_t size,
                                RawpanelStatusState *semantic, FILE *err);
int rawpanel_status_alsa_control(const RawpanelStatusSourceConfig *config,
                                 RawpanelStatusControl control, FILE *err);

#endif

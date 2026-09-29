/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_STATUS_WORKER_H
#define RAWPANEL_STATUS_WORKER_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <stdio.h>
#include <sys/types.h>

typedef struct {
  int fd;
  pid_t pid;
} RawpanelStatusWorker;

void rawpanel_status_worker_init(RawpanelStatusWorker *worker);
int rawpanel_status_worker_start(RawpanelStatusWorker *worker,
                             const RawpanelStatusConfig *config, FILE *err);
void rawpanel_status_worker_stop(RawpanelStatusWorker *worker, FILE *err);
int rawpanel_status_worker_receive(RawpanelStatusWorker *worker,
                               RawpanelStatusData *status, FILE *err);
int rawpanel_status_worker_control(RawpanelStatusWorker *worker,
                               RawpanelStatusControl control, FILE *err);
int rawpanel_status_worker_debug(const RawpanelStatusConfig *config, FILE *out,
                             FILE *err);

#endif

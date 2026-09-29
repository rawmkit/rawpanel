/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_RAWM_H
#define RAWPANEL_RAWM_H

#include <X11/Xlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RAWPANEL_MAX_MONITORS 16
#define RAWPANEL_MAX_TAGS 32

typedef struct {
  int number;
  bool selected;
  int x, y, width, height;
  uint32_t viewed, occupied, urgent;
  char *layout;
  Window focused;
  int nmaster;
  double mfact;
  unsigned int focused_index;
  unsigned int visible_count;
  char *tags[RAWPANEL_MAX_TAGS];
} RawpanelRawmMonitor;

typedef struct {
  unsigned int tag_count;
  int selected_monitor;
  RawpanelRawmMonitor *monitors;
  size_t monitor_count;
} RawpanelRawmState;

typedef struct {
  Display *display;
  int screen;
  Window owner;
  Window requestor;
  Atom utf8;
  Atom selection;
  Atom version;
  Atom supported;
  Atom state;
  Atom command;
  Atom command_text;
} RawpanelRawm;

void rawpanel_rawm_state_destroy(RawpanelRawmState *state);
int rawpanel_rawm_init(RawpanelRawm *rawm, Display *display, int screen, FILE *err);
void rawpanel_rawm_destroy(RawpanelRawm *rawm);
int rawpanel_rawm_discover(RawpanelRawm *rawm, FILE *err);
int rawpanel_rawm_read_state(RawpanelRawm *rawm, RawpanelRawmState *state, FILE *err);
int rawpanel_rawm_send(RawpanelRawm *rawm, const char *name,
                       int32_t arg0, int32_t arg1, FILE *err);
int rawpanel_rawm_layout_id(const char *name);

#endif

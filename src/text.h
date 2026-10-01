/* SPDX-License-Identifier: MIT */
#ifndef RAWPANEL_TEXT_H
#define RAWPANEL_TEXT_H

#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include <stdbool.h>
#include <stddef.h>

#define RAWPANEL_TEXT_FALLBACK_MAX 32u
#define RAWPANEL_TEXT_MISSING_MAX 128u

typedef struct {
  Display *display;
  int screen;
  XftFont *primary;
  FcPattern *pattern;
  XftFont *fallbacks[RAWPANEL_TEXT_FALLBACK_MAX];
  size_t fallback_count;
  FcChar32 missing[RAWPANEL_TEXT_MISSING_MAX];
} RawpanelText;

int rawpanel_text_init(RawpanelText *text, Display *display, int screen,
                       const char *font_name);
void rawpanel_text_destroy(RawpanelText *text);

int rawpanel_text_width(RawpanelText *text, const char *value);
int rawpanel_text_width_n(RawpanelText *text, const char *value, size_t length);
void rawpanel_text_draw(RawpanelText *text, XftDraw *draw, XftColor *color,
                        int x, int y, int height,
                        const char *value, size_t length);


#endif

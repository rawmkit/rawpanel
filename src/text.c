/* SPDX-License-Identifier: MIT */
#include "text.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t
utf8_decode(const char *value, size_t length, FcChar32 *codepoint)
{
  int consumed;

  if (!value || !length || !codepoint)
    return 0;

  consumed = FcUtf8ToUcs4((const FcChar8 *)value, codepoint,
                          length > INT_MAX ? INT_MAX : (int)length);
  if (consumed <= 0 || (size_t)consumed > length) {
    *codepoint = 0xfffdu;
    return 1;
  }

  return (size_t)consumed;
}

static void
missing_indexes(FcChar32 codepoint, size_t *first, size_t *second)
{
  uint32_t hash = (uint32_t)codepoint;

  hash = ((hash >> 16) ^ hash) * UINT32_C(0x21f0aaad);
  hash = ((hash >> 15) ^ hash) * UINT32_C(0xd35a2d97);
  hash = (hash >> 15) ^ hash;

  *first = hash % RAWPANEL_TEXT_MISSING_MAX;
  *second = (hash >> 17) % RAWPANEL_TEXT_MISSING_MAX;
}

static bool
missing_known(const RawpanelText *text, FcChar32 codepoint)
{
  size_t first, second;

  missing_indexes(codepoint, &first, &second);
  return text->missing[first] == codepoint ||
         text->missing[second] == codepoint;
}

static void
remember_missing(RawpanelText *text, FcChar32 codepoint)
{
  size_t first, second;

  missing_indexes(codepoint, &first, &second);
  text->missing[text->missing[first] ? second : first] = codepoint;
}

static XftFont *
font_for_codepoint(RawpanelText *text, FcChar32 codepoint)
{
  FcCharSet *charset;
  FcPattern *match;
  FcPattern *pattern;
  FcResult result;
  XftFont *font;
  size_t i;

  if (!text || !text->display || !text->primary)
    return NULL;

  if (XftCharExists(text->display, text->primary, codepoint))
    return text->primary;

  for (i = 0; i < text->fallback_count; ++i)
    if (XftCharExists(text->display, text->fallbacks[i], codepoint))
      return text->fallbacks[i];

  if (!text->pattern || missing_known(text, codepoint) ||
      text->fallback_count >= RAWPANEL_TEXT_FALLBACK_MAX)
    return text->primary;

  charset = FcCharSetCreate();
  pattern = FcPatternDuplicate(text->pattern);
  if (!charset || !pattern) {
    if (charset)
      FcCharSetDestroy(charset);
    if (pattern)
      FcPatternDestroy(pattern);
    remember_missing(text, codepoint);
    return text->primary;
  }

  FcCharSetAddChar(charset, codepoint);
  FcPatternDel(pattern, FC_CHARSET);
  FcPatternAddCharSet(pattern, FC_CHARSET, charset);

  /*
   * Keep fallback faces inside Xft's dependable outline-rendering path.
   * Fixed bitmap strikes can exceed the panel line metrics, while modern
   * color fonts can advertise glyph coverage that Xft cannot rasterize.
   */
  FcPatternDel(pattern, FC_SCALABLE);
  FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
  FcPatternDel(pattern, FC_COLOR);
  FcPatternAddBool(pattern, FC_COLOR, FcFalse);
  FcConfigSubstitute(NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  match = XftFontMatch(text->display, text->screen, pattern, &result);
  FcPatternDestroy(pattern);
  FcCharSetDestroy(charset);

  if (!match) {
    remember_missing(text, codepoint);
    return text->primary;
  }

  font = XftFontOpenPattern(text->display, match);
  if (!font) {
    FcPatternDestroy(match);
    remember_missing(text, codepoint);
    return text->primary;
  }

  if (!XftCharExists(text->display, font, codepoint)) {
    XftFontClose(text->display, font);
    remember_missing(text, codepoint);
    return text->primary;
  }

  text->fallbacks[text->fallback_count++] = font;
  return font;
}

static int
font_width(Display *display, XftFont *font, const char *value, size_t length)
{
  XGlyphInfo extents;
  int n;

  if (!display || !font || !value || !length)
    return 0;

  n = length > INT_MAX ? INT_MAX : (int)length;
  XftTextExtentsUtf8(display, font, (const FcChar8 *)value, n, &extents);
  return (int)extents.xOff;
}

int
rawpanel_text_width_n(RawpanelText *text, const char *value, size_t length)
{
  size_t pos = 0;
  int width = 0;

  if (!text || !value || !length)
    return 0;

  while (pos < length) {
    FcChar32 codepoint;
    XftFont *font;
    size_t next;
    size_t run_start = pos;

    next = utf8_decode(value + pos, length - pos, &codepoint);
    if (!next)
      break;
    font = font_for_codepoint(text, codepoint);
    if (!font)
      break;
    pos += next;

    while (pos < length) {
      XftFont *next_font;

      next = utf8_decode(value + pos, length - pos, &codepoint);
      if (!next)
        break;
      next_font = font_for_codepoint(text, codepoint);
      if (next_font != font)
        break;
      pos += next;
    }

    width += font_width(text->display, font, value + run_start,
                        pos - run_start);
  }

  return width;
}

int
rawpanel_text_width(RawpanelText *text, const char *value)
{
  return value ? rawpanel_text_width_n(text, value, strlen(value)) : 0;
}

void
rawpanel_text_draw(RawpanelText *text, XftDraw *draw, XftColor *color,
                   int x, int y, int height,
                   const char *value, size_t length)
{
  size_t pos = 0;

  if (!text || !draw || !color || !value || !length)
    return;

  while (pos < length) {
    FcChar32 codepoint;
    XftFont *font;
    size_t next;
    size_t run_start = pos;

    next = utf8_decode(value + pos, length - pos, &codepoint);
    if (!next)
      break;
    font = font_for_codepoint(text, codepoint);
    if (!font)
      break;
    pos += next;

    while (pos < length) {
      XftFont *next_font;

      next = utf8_decode(value + pos, length - pos, &codepoint);
      if (!next)
        break;
      next_font = font_for_codepoint(text, codepoint);
      if (next_font != font)
        break;
      pos += next;
    }

    {
      int font_height = font->ascent + font->descent;
      int baseline = y + (height - font_height) / 2 + font->ascent;

      XftDrawStringUtf8(draw, color, font, x, baseline,
                        (const FcChar8 *)value + run_start,
                        (int)(pos - run_start));
    }
    x += font_width(text->display, font, value + run_start, pos - run_start);
  }
}

int
rawpanel_text_init(RawpanelText *text, Display *display, int screen,
                   const char *font_name)
{
  const char *pattern_name = font_name;

  if (!text || !display || !font_name || !*font_name)
    return -1;

  memset(text, 0, sizeof(*text));
  text->display = display;
  text->screen = screen;
  text->primary = XftFontOpenName(display, screen, font_name);
  if (!text->primary) {
    pattern_name = "fixed";
    text->primary = XftFontOpenName(display, screen, pattern_name);
  }
  if (!text->primary)
    goto fail;

  text->pattern = FcNameParse((const FcChar8 *)pattern_name);
  if (!text->pattern)
    goto fail;

  return 0;

fail:
  rawpanel_text_destroy(text);
  return -1;
}

void
rawpanel_text_destroy(RawpanelText *text)
{
  size_t i;

  if (!text)
    return;

  if (text->display) {
    for (i = 0; i < text->fallback_count; ++i)
      if (text->fallbacks[i])
        XftFontClose(text->display, text->fallbacks[i]);
    if (text->primary)
      XftFontClose(text->display, text->primary);
  }
  if (text->pattern)
    FcPatternDestroy(text->pattern);

  memset(text, 0, sizeof(*text));
}

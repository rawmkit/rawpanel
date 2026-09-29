/* SPDX-License-Identifier: MIT */

#include "status.h"

#include <stdlib.h>
#include <string.h>

static char *
copy_bytes(const unsigned char *text, size_t length)
{
  char *copy = malloc(length + 1);

  if (!copy)
    return NULL;
  if (length)
    memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

int
rawpanel_status_utf8_valid(const unsigned char *text, size_t length)
{
  size_t i = 0;

  if (!text && length)
    return 0;
  while (i < length) {
    unsigned char c = text[i++];

    if (c <= 0x7f)
      continue;
    if (c >= 0xc2 && c <= 0xdf) {
      if (i >= length || text[i] < 0x80 || text[i] > 0xbf)
        return 0;
      ++i;
      continue;
    }
    if (c == 0xe0) {
      if (i + 1 >= length || text[i] < 0xa0 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return 0;
      i += 2;
      continue;
    }
    if ((c >= 0xe1 && c <= 0xec) || (c >= 0xee && c <= 0xef)) {
      if (i + 1 >= length || text[i] < 0x80 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return 0;
      i += 2;
      continue;
    }
    if (c == 0xed) {
      if (i + 1 >= length || text[i] < 0x80 || text[i] > 0x9f ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return 0;
      i += 2;
      continue;
    }
    if (c == 0xf0) {
      if (i + 2 >= length || text[i] < 0x90 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return 0;
      i += 3;
      continue;
    }
    if (c >= 0xf1 && c <= 0xf3) {
      if (i + 2 >= length || text[i] < 0x80 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return 0;
      i += 3;
      continue;
    }
    if (c == 0xf4) {
      if (i + 2 >= length || text[i] < 0x80 || text[i] > 0x8f ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return 0;
      i += 3;
      continue;
    }
    return 0;
  }
  return 1;
}

int
rawpanel_status_id_valid(const unsigned char *id, size_t length)
{
  size_t i;

  if (!id || !length || length > RAWPANEL_STATUS_ID_MAX)
    return 0;
  for (i = 0; i < length; ++i) {
    unsigned char c = id[i];

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
      continue;
    return 0;
  }
  return 1;
}

int
rawpanel_status_printable_utf8_valid(const unsigned char *text, size_t length)
{
  size_t i;

  if ((!text && length) || !rawpanel_status_utf8_valid(text, length))
    return 0;
  for (i = 0; i < length; ++i)
    if (text[i] < 0x20 || text[i] == 0x7f)
      return 0;
  return 1;
}

int
rawpanel_status_text_valid(const unsigned char *text, size_t length)
{
  return length && rawpanel_status_printable_utf8_valid(text, length);
}

const char *
rawpanel_status_state_name(RawpanelStatusState state)
{
  switch (state) {
  case RAWPANEL_STATUS_WARNING:
    return "warning";
  case RAWPANEL_STATUS_CRITICAL:
    return "critical";
  case RAWPANEL_STATUS_STALE:
    return "stale";
  case RAWPANEL_STATUS_NORMAL:
    return "normal";
  }
  return NULL;
}

int
rawpanel_status_state_parse(const unsigned char *text, size_t length,
                        RawpanelStatusState *state)
{
  if (!text || !state)
    return 0;
  if (length == 6 && !memcmp(text, "normal", 6))
    *state = RAWPANEL_STATUS_NORMAL;
  else if (length == 7 && !memcmp(text, "warning", 7))
    *state = RAWPANEL_STATUS_WARNING;
  else if (length == 8 && !memcmp(text, "critical", 8))
    *state = RAWPANEL_STATUS_CRITICAL;
  else if (length == 5 && !memcmp(text, "stale", 5))
    *state = RAWPANEL_STATUS_STALE;
  else
    return 0;
  return 1;
}

void
rawpanel_status_data_destroy(RawpanelStatusData *status)
{
  size_t i;

  if (!status)
    return;
  for (i = 0; i < status->count; ++i) {
    free(status->segments[i].id);
    free(status->segments[i].text);
  }
  free(status->segments);
  status->segments = NULL;
  status->count = 0;
}

static int
contains_id(const RawpanelStatusSegment *segments, size_t count,
            const unsigned char *id, size_t id_len)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (strlen(segments[i].id) == id_len &&
        !memcmp(segments[i].id, id, id_len))
      return 1;
  return 0;
}

RawpanelStatusResult
rawpanel_status_snapshot_parse(const unsigned char *value, size_t length,
                     RawpanelStatusData *status)
{
  RawpanelStatusData parsed = {0};
  size_t pos = 0;

  if (!status)
    return RAWPANEL_STATUS_INVALID;
  if (!length) {
    *status = parsed;
    return RAWPANEL_STATUS_OK;
  }
  if (!value || length > RAWPANEL_STATUS_SNAPSHOT_MAX)
    return length > RAWPANEL_STATUS_SNAPSHOT_MAX ? RAWPANEL_STATUS_TOO_LARGE
                                       : RAWPANEL_STATUS_INVALID;
  if (value[length - 1] != '\n')
    return RAWPANEL_STATUS_INVALID;

  while (pos < length) {
    const unsigned char *line = value + pos;
    const unsigned char *newline = memchr(line, '\n', length - pos);
    const unsigned char *first, *second;
    size_t line_len, id_len, state_len, text_len;
    RawpanelStatusSegment *segments, *segment;
    RawpanelStatusState state;

    if (!newline)
      goto invalid;
    line_len = (size_t)(newline - line);
    first = memchr(line, '\t', line_len);
    if (!first)
      goto invalid;
    second = memchr(first + 1, '\t', line_len - (size_t)(first + 1 - line));
    if (!second)
      goto invalid;

    id_len = (size_t)(first - line);
    state_len = (size_t)(second - first - 1);
    text_len = line_len - (size_t)(second + 1 - line);
    if (!rawpanel_status_id_valid(line, id_len) ||
        !rawpanel_status_state_parse(first + 1, state_len, &state) ||
        !rawpanel_status_text_valid(second + 1, text_len) ||
        contains_id(parsed.segments, parsed.count, line, id_len))
      goto invalid;

    segments = realloc(parsed.segments,
                       (parsed.count + 1) * sizeof(*parsed.segments));
    if (!segments)
      goto no_memory;
    parsed.segments = segments;
    segment = &parsed.segments[parsed.count];
    memset(segment, 0, sizeof(*segment));
    segment->id = copy_bytes(line, id_len);
    segment->text = copy_bytes(second + 1, text_len);
    if (!segment->id || !segment->text) {
      free(segment->id);
      free(segment->text);
      segment->id = NULL;
      segment->text = NULL;
      goto no_memory;
    }
    segment->state = state;
    parsed.count++;
    pos += line_len + 1;
  }

  *status = parsed;
  return RAWPANEL_STATUS_OK;

invalid:
  rawpanel_status_data_destroy(&parsed);
  return RAWPANEL_STATUS_INVALID;
no_memory:
  rawpanel_status_data_destroy(&parsed);
  return RAWPANEL_STATUS_NO_MEMORY;
}

RawpanelStatusResult
rawpanel_status_snapshot_encode(const RawpanelStatusView *segments, size_t count,
                      char **value, size_t *length)
{
  size_t i, j, total = 0;
  char *encoded;

  if (!value || !length || (count && !segments))
    return RAWPANEL_STATUS_INVALID;
  *value = NULL;
  *length = 0;

  for (i = 0; i < count; ++i) {
    const char *state_name;
    size_t id_len, state_len, text_len, extra;

    if (!segments[i].id || !segments[i].text)
      return RAWPANEL_STATUS_INVALID;
    id_len = strlen(segments[i].id);
    text_len = strlen(segments[i].text);
    state_name = rawpanel_status_state_name(segments[i].state);
    if (!state_name ||
        !rawpanel_status_id_valid((const unsigned char *)segments[i].id, id_len) ||
        !rawpanel_status_text_valid((const unsigned char *)segments[i].text,
                                text_len))
      return RAWPANEL_STATUS_INVALID;
    if (text_len > RAWPANEL_STATUS_SNAPSHOT_MAX)
      return RAWPANEL_STATUS_TOO_LARGE;
    for (j = 0; j < i; ++j)
      if (!strcmp(segments[j].id, segments[i].id))
        return RAWPANEL_STATUS_INVALID;
    state_len = strlen(state_name);
    extra = id_len + 1 + state_len + 1 + text_len + 1;
    if (extra > RAWPANEL_STATUS_SNAPSHOT_MAX - total)
      return RAWPANEL_STATUS_TOO_LARGE;
    total += extra;
  }

  encoded = malloc(total + 1);
  if (!encoded)
    return RAWPANEL_STATUS_NO_MEMORY;
  total = 0;
  for (i = 0; i < count; ++i) {
    const char *state_name = rawpanel_status_state_name(segments[i].state);
    size_t id_len = strlen(segments[i].id);
    size_t state_len = strlen(state_name);
    size_t text_len = strlen(segments[i].text);

    memcpy(encoded + total, segments[i].id, id_len);
    total += id_len;
    encoded[total++] = '\t';
    memcpy(encoded + total, state_name, state_len);
    total += state_len;
    encoded[total++] = '\t';
    memcpy(encoded + total, segments[i].text, text_len);
    total += text_len;
    encoded[total++] = '\n';
  }
  encoded[total] = '\0';
  *value = encoded;
  *length = total;
  return RAWPANEL_STATUS_OK;
}

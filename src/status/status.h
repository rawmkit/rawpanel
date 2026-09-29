#ifndef RAWPANEL_STATUS_H
#define RAWPANEL_STATUS_H

#include <stddef.h>

#define RAWPANEL_STATUS_SNAPSHOT_MAX 4096u
#define RAWPANEL_STATUS_ID_MAX 64u

typedef enum {
  RAWPANEL_STATUS_NORMAL,
  RAWPANEL_STATUS_WARNING,
  RAWPANEL_STATUS_CRITICAL,
  RAWPANEL_STATUS_STALE
} RawpanelStatusState;

typedef struct {
  char *id;
  char *text;
  RawpanelStatusState state;
} RawpanelStatusSegment;

typedef struct {
  RawpanelStatusSegment *segments;
  size_t count;
} RawpanelStatusData;

typedef struct {
  const char *id;
  const char *text;
  RawpanelStatusState state;
} RawpanelStatusView;

typedef enum {
  RAWPANEL_STATUS_OK = 0,
  RAWPANEL_STATUS_INVALID,
  RAWPANEL_STATUS_TOO_LARGE,
  RAWPANEL_STATUS_NO_MEMORY
} RawpanelStatusResult;

int rawpanel_status_utf8_valid(const unsigned char *text, size_t length);
int rawpanel_status_id_valid(const unsigned char *id, size_t length);
int rawpanel_status_printable_utf8_valid(const unsigned char *text, size_t length);
int rawpanel_status_text_valid(const unsigned char *text, size_t length);
const char *rawpanel_status_state_name(RawpanelStatusState state);
int rawpanel_status_state_parse(const unsigned char *text, size_t length,
                            RawpanelStatusState *state);
void rawpanel_status_data_destroy(RawpanelStatusData *status);
RawpanelStatusResult rawpanel_status_snapshot_parse(const unsigned char *value,
                                            size_t length,
                                            RawpanelStatusData *status);
RawpanelStatusResult rawpanel_status_snapshot_encode(const RawpanelStatusView *segments,
                                             size_t count, char **value,
                                             size_t *length);

#endif

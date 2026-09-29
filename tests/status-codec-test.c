#include "status/status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "status-codec-test: %s\n", message);
  exit(EXIT_FAILURE);
}

static void
expect_result(RawpanelStatusResult got, RawpanelStatusResult expected,
              const char *message)
{
  if (got != expected)
    fail(message);
}

int
main(void)
{
  static const unsigned char valid[] =
    "clock\tnormal\t12:34\n"
    "battery\twarning\t12%\n"
    "network\tstale\tnet ?\n";
  static const unsigned char duplicate[] =
    "clock\tnormal\tone\nclock\tcritical\ttwo\n";
  static const unsigned char bad_state[] = "clock\tbroken\ttext\n";
  static const unsigned char bad_id[] = "not an id\tnormal\ttext\n";
  static const unsigned char bad_text[] = "clock\tnormal\tbad\ttext\n";
  static const unsigned char bad_utf8[] = {
    'x', '\t', 'n', 'o', 'r', 'm', 'a', 'l', '\t', 0xc0, 0x80, '\n'
  };
  RawpanelStatusData parsed = {0};
  RawpanelStatusView segments[3] = {
    { .id = "clock", .text = "12:34", .state = RAWPANEL_STATUS_NORMAL },
    { .id = "battery", .text = "12%", .state = RAWPANEL_STATUS_WARNING },
    { .id = "network", .text = "net ?", .state = RAWPANEL_STATUS_STALE },
  };
  char *encoded = NULL;
  char *large_text;
  size_t length = 0;
  unsigned char *boundary;
  size_t prefix_len = strlen("x\tnormal\t");
  size_t text_len = RAWPANEL_STATUS_SNAPSHOT_MAX - prefix_len - 1;

  expect_result(rawpanel_status_snapshot_parse(valid, sizeof(valid) - 1, &parsed),
                RAWPANEL_STATUS_OK, "valid payload rejected");
  if (parsed.count != 3 || strcmp(parsed.segments[0].id, "clock") ||
      parsed.segments[0].state != RAWPANEL_STATUS_NORMAL ||
      strcmp(parsed.segments[1].text, "12%") ||
      parsed.segments[2].state != RAWPANEL_STATUS_STALE)
    fail("valid payload decoded incorrectly");
  rawpanel_status_data_destroy(&parsed);

  expect_result(rawpanel_status_snapshot_encode(segments, 3, &encoded, &length),
                RAWPANEL_STATUS_OK, "valid segments did not encode");
  if (length != sizeof(valid) - 1 || memcmp(encoded, valid, length))
    fail("encoded payload differs from canonical wire form");
  free(encoded);

  expect_result(rawpanel_status_snapshot_parse(NULL, 0, &parsed), RAWPANEL_STATUS_OK,
                "empty payload rejected");
  if (parsed.count)
    fail("empty payload produced segments");

  expect_result(rawpanel_status_snapshot_parse(duplicate, sizeof(duplicate) - 1, &parsed),
                RAWPANEL_STATUS_INVALID, "duplicate id accepted");
  expect_result(rawpanel_status_snapshot_parse(bad_state, sizeof(bad_state) - 1, &parsed),
                RAWPANEL_STATUS_INVALID, "unknown state accepted");
  expect_result(rawpanel_status_snapshot_parse(bad_id, sizeof(bad_id) - 1, &parsed),
                RAWPANEL_STATUS_INVALID, "invalid id accepted");
  expect_result(rawpanel_status_snapshot_parse(bad_text, sizeof(bad_text) - 1, &parsed),
                RAWPANEL_STATUS_INVALID, "control text accepted");
  expect_result(rawpanel_status_snapshot_parse(bad_utf8, sizeof(bad_utf8), &parsed),
                RAWPANEL_STATUS_INVALID, "invalid UTF-8 accepted");

  segments[1].id = "clock";
  expect_result(rawpanel_status_snapshot_encode(segments, 3, &encoded, &length),
                RAWPANEL_STATUS_INVALID, "duplicate encoded id accepted");
  segments[1].id = "battery";

  large_text = malloc(RAWPANEL_STATUS_SNAPSHOT_MAX + 1);
  if (!large_text)
    fail("allocation failed");
  memset(large_text, 'x', RAWPANEL_STATUS_SNAPSHOT_MAX);
  large_text[RAWPANEL_STATUS_SNAPSHOT_MAX] = '\0';
  segments[0].text = large_text;
  expect_result(rawpanel_status_snapshot_encode(segments, 1, &encoded, &length),
                RAWPANEL_STATUS_TOO_LARGE, "oversized encoded text accepted");
  free(large_text);
  segments[0].text = "12:34";

  boundary = malloc(RAWPANEL_STATUS_SNAPSHOT_MAX + 1);
  if (!boundary)
    fail("allocation failed");
  memcpy(boundary, "x\tnormal\t", prefix_len);
  memset(boundary + prefix_len, 'a', text_len);
  boundary[RAWPANEL_STATUS_SNAPSHOT_MAX - 1] = '\n';
  boundary[RAWPANEL_STATUS_SNAPSHOT_MAX] = '\0';
  expect_result(rawpanel_status_snapshot_parse(boundary, RAWPANEL_STATUS_SNAPSHOT_MAX, &parsed),
                RAWPANEL_STATUS_OK, "maximum-size payload rejected");
  rawpanel_status_data_destroy(&parsed);
  expect_result(rawpanel_status_snapshot_parse(boundary, RAWPANEL_STATUS_SNAPSHOT_MAX + 1, &parsed),
                RAWPANEL_STATUS_TOO_LARGE, "oversized payload accepted");
  free(boundary);

  return EXIT_SUCCESS;
}

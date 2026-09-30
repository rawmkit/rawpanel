/* SPDX-License-Identifier: MIT */
#include "rawm.h"

#include <X11/Xatom.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RAWM_CONTROL_VERSION 1u

static char *dupbytes(const char *s, size_t n)
{
  char *p = malloc(n + 1u);
  if (!p)
    return NULL;
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

static int escaped_text_valid(const char *text);

static int hexval(unsigned char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  return -1;
}

static char *unescape(const char *s)
{
  size_t n, i, j = 0;

  if (!escaped_text_valid(s))
    return NULL;
  n = strlen(s);
  char *out = malloc(n + 1u);
  if (!out)
    return NULL;
  for (i = 0; i < n; ++i) {
    if (s[i] == '%' && i + 2u < n) {
      int hi = hexval((unsigned char)s[i + 1u]);
      int lo = hexval((unsigned char)s[i + 2u]);
      if (hi >= 0 && lo >= 0) {
        out[j++] = (char)((hi << 4) | lo);
        i += 2u;
        continue;
      }
    }
    out[j++] = s[i];
  }
  out[j] = '\0';
  return out;
}

void rawpanel_rawm_state_destroy(RawpanelRawmState *state)
{
  size_t i, j;
  if (!state)
    return;
  for (i = 0; i < state->monitor_count; ++i) {
    free(state->monitors[i].layout);
    free(state->monitors[i].title);
    for (j = 0; j < RAWPANEL_MAX_TAGS; ++j)
      free(state->monitors[i].tags[j]);
  }
  free(state->monitors);
  memset(state, 0, sizeof(*state));
}

int rawpanel_rawm_init(RawpanelRawm *rawm, Display *display, int screen, FILE *err)
{
  char name[64];
  int n;
  XSetWindowAttributes attrs;
  if (!rawm || !display || screen < 0)
    return -1;
  memset(rawm, 0, sizeof(*rawm));
  rawm->display = display;
  rawm->screen = screen;
  rawm->utf8 = XInternAtom(display, "UTF8_STRING", False);
  rawm->version = XInternAtom(display, "_RAWM_CONTROL_VERSION", False);
  rawm->supported = XInternAtom(display, "_RAWM_SUPPORTED_COMMANDS", False);
  rawm->state = XInternAtom(display, "_RAWM_STATE", False);
  rawm->clients = XInternAtom(display, "_RAWM_CLIENTS", False);
  rawm->command = XInternAtom(display, "_RAWM_COMMAND", False);
  rawm->command_text = XInternAtom(display, "_RAWM_COMMAND_TEXT", False);
  n = snprintf(name, sizeof(name), "_RAWM_CONTROL_S%d", screen);
  if (n < 0 || (size_t)n >= sizeof(name))
    return -1;
  rawm->selection = XInternAtom(display, name, False);
  if (!rawm->utf8 || !rawm->version || !rawm->supported || !rawm->state ||
      !rawm->clients || !rawm->command || !rawm->command_text || !rawm->selection)
    return -1;
  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  rawm->requestor = XCreateWindow(display, RootWindow(display, screen),
                                  -1, -1, 1, 1, 0, 0, InputOnly, CopyFromParent,
                                  CWOverrideRedirect, &attrs);
  if (!rawm->requestor) {
    if (err) fputs("rawpanel: could not create rawm request window\n", err);
    return -1;
  }
  return 0;
}

void rawpanel_rawm_destroy(RawpanelRawm *rawm)
{
  if (!rawm)
    return;
  if (rawm->display && rawm->requestor)
    XDestroyWindow(rawm->display, rawm->requestor);
  memset(rawm, 0, sizeof(*rawm));
}

static int get_cardinal(Display *dpy, Window win, Atom atom, unsigned long *value)
{
  Atom type = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;
  int rc;
  rc = XGetWindowProperty(dpy, win, atom, 0, 1, False, XA_CARDINAL,
                          &type, &format, &nitems, &after, &data);
  if (rc != Success || type != XA_CARDINAL || format != 32 || nitems != 1 || after != 0) {
    if (data) XFree(data);
    return -1;
  }
  *value = *(unsigned long *)data;
  XFree(data);
  return 0;
}

int rawpanel_rawm_discover(RawpanelRawm *rawm, FILE *err)
{
  unsigned long version;
  Window owner;
  if (!rawm || !rawm->display)
    return -1;
  owner = XGetSelectionOwner(rawm->display, rawm->selection);
  if (!owner) {
    rawm->owner = None;
    return 0;
  }
  if (get_cardinal(rawm->display, owner, rawm->version, &version) < 0 ||
      version != RAWM_CONTROL_VERSION) {
    if (err) fputs("rawpanel: incompatible rawm control authority\n", err);
    rawm->owner = None;
    return -1;
  }
  rawm->owner = owner;
  XSelectInput(rawm->display, owner, PropertyChangeMask | StructureNotifyMask);
  return 1;
}

static int read_property(RawpanelRawm *rawm, Atom property,
                         unsigned char **data, unsigned long *length)
{
  Atom type = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *value = NULL;
  int rc;
  rc = XGetWindowProperty(rawm->display, rawm->owner, property,
                          0, 1024 * 1024, False, rawm->utf8,
                          &type, &format, &nitems, &after, &value);
  if (rc != Success || type != rawm->utf8 || format != 8 || after != 0) {
    if (value) XFree(value);
    return -1;
  }
  *data = value;
  *length = nitems;
  return 0;
}

static int layout_id(const char *name)
{
  static const char *const names[] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  size_t i;

  if (!name)
    return -1;
  for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if (!strcmp(name, names[i]))
      return (int)i;
  return -1;
}

static int
split_fields(char *line, char **fields, size_t expected)
{
  size_t count = 0;
  char *p;

  if (!line || !fields || !expected)
    return -1;
  fields[count++] = line;
  for (p = line; *p; ++p) {
    if (*p != '\t')
      continue;
    *p = '\0';
    if (count >= expected)
      return -1;
    fields[count++] = p + 1;
  }
  return count == expected ? 0 : -1;
}

static int
parse_ulong_field(const char *text, int base, unsigned long max,
                  unsigned long *value)
{
  char *end = NULL;
  unsigned long parsed;

  if (!text || !*text || text[0] == '-' || text[0] == '+')
    return -1;
  errno = 0;
  parsed = strtoul(text, &end, base);
  if (errno || !end || end == text || *end || parsed > max)
    return -1;
  *value = parsed;
  return 0;
}

static int
parse_long_field(const char *text, long min, long max, long *value)
{
  char *end = NULL;
  long parsed;

  if (!text || !*text || text[0] == '+')
    return -1;
  errno = 0;
  parsed = strtol(text, &end, 10);
  if (errno || !end || end == text || *end || parsed < min || parsed > max)
    return -1;
  *value = parsed;
  return 0;
}

static int
parse_mask_field(const char *text, uint32_t *value)
{
  uint32_t parsed = 0;
  size_t i;

  if (!text || strlen(text) != 8u)
    return -1;
  for (i = 0; i < 8u; ++i) {
    int digit = hexval((unsigned char)text[i]);

    if (digit < 0)
      return -1;
    parsed = (parsed << 4) | (uint32_t)digit;
  }
  *value = parsed;
  return 0;
}

static int
parse_xid_field(const char *text, bool allow_zero, Window *value)
{
  unsigned long parsed;

  if (!text || text[0] != '0' || text[1] != 'x' || !text[2] ||
      parse_ulong_field(text + 2, 16, UINT32_MAX, &parsed) < 0 ||
      (!allow_zero && parsed == 0))
    return -1;
  *value = (Window)parsed;
  return 0;
}

static int escaped_text_valid(const char *text)
{
  size_t i;

  if (!text)
    return 0;
  for (i = 0; text[i]; ++i)
  {
    if (text[i] == '%')
    {
      int hi;
      int lo;

      if (!text[i + 1u] || !text[i + 2u])
        return 0;
      hi = hexval((unsigned char)text[i + 1u]);
      lo = hexval((unsigned char)text[i + 2u]);
      if (hi < 0 || lo < 0 || ((hi << 4) | lo) == 0)
        return 0;
      i += 2u;
    }
  }
  return 1;
}

static RawpanelRawmMonitor *monitor_by_number(RawpanelRawmState *state, int number)
{
  size_t i;
  for (i = 0; i < state->monitor_count; ++i)
    if (state->monitors[i].number == number)
      return &state->monitors[i];
  return NULL;
}

static int read_client_titles(RawpanelRawm *rawm, RawpanelRawmState *state,
                              FILE *err);

int rawpanel_rawm_read_state(RawpanelRawm *rawm, RawpanelRawmState *state, FILE *err)
{
  unsigned char *property = NULL;
  unsigned long length = 0;
  char *text = NULL, *save = NULL, *line;
  RawpanelRawmState parsed = {0};
  bool header = false;
  bool tag_count_seen = false;
  bool selected_monitor_seen = false;

  if (!rawm || !state || !rawm->owner)
    return -1;
  if (read_property(rawm, rawm->state, &property, &length) < 0)
    return -1;
  if (length && memchr(property, '\0', length)) {
    XFree(property);
    return -1;
  }
  text = dupbytes((const char *)property, (size_t)length);
  XFree(property);
  if (!text)
    return -1;

  for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    if (!header) {
      if (strcmp(line, "rawm-state-v2")) goto invalid;
      header = true;
      continue;
    }
    if (!strncmp(line, "tag-count\t", 10)) {
      unsigned long v;
      if (tag_count_seen ||
          parse_ulong_field(line + 10, 10, RAWPANEL_MAX_TAGS, &v) < 0 || v == 0)
        goto invalid;
      parsed.tag_count = (unsigned int)v;
      tag_count_seen = true;
      continue;
    }
    if (!strncmp(line, "selected-monitor\t", 17)) {
      long v;
      if (selected_monitor_seen ||
          parse_long_field(line + 17, -1, INT_MAX, &v) < 0)
        goto invalid;
      parsed.selected_monitor = (int)v;
      selected_monitor_seen = true;
      continue;
    }
    if (!strncmp(line, "monitor\t", 8)) {
      RawpanelRawmMonitor m = {0};
      RawpanelRawmMonitor *monitors;
      char *fields[18];
      long number, x, y, width, height, work_x, work_y, work_width, work_height, nmaster;
      unsigned long selected;
      char *end = NULL;

      if (!tag_count_seen || parsed.monitor_count >= RAWPANEL_MAX_MONITORS ||
          split_fields(line, fields, 18u) < 0 || strcmp(fields[0], "monitor") ||
          parse_long_field(fields[1], 0, INT_MAX, &number) < 0 ||
          parse_ulong_field(fields[2], 10, 1, &selected) < 0 ||
          parse_long_field(fields[3], INT_MIN, INT_MAX, &x) < 0 ||
          parse_long_field(fields[4], INT_MIN, INT_MAX, &y) < 0 ||
          parse_long_field(fields[5], 1, INT_MAX, &width) < 0 ||
          parse_long_field(fields[6], 1, INT_MAX, &height) < 0 ||
          parse_long_field(fields[7], INT_MIN, INT_MAX, &work_x) < 0 ||
          parse_long_field(fields[8], INT_MIN, INT_MAX, &work_y) < 0 ||
          parse_long_field(fields[9], 1, INT_MAX, &work_width) < 0 ||
          parse_long_field(fields[10], 1, INT_MAX, &work_height) < 0 ||
          parse_mask_field(fields[11], &m.viewed) < 0 ||
          parse_mask_field(fields[12], &m.occupied) < 0 ||
          parse_mask_field(fields[13], &m.urgent) < 0 ||
          layout_id(fields[14]) < 0 ||
          parse_xid_field(fields[15], true, &m.focused) < 0 ||
          parse_long_field(fields[16], 0, INT_MAX, &nmaster) < 0)
        goto invalid;
      errno = 0;
      m.mfact = strtod(fields[17], &end);
      if (errno || !end || end == fields[17] || *end || !isfinite(m.mfact) ||
          m.mfact < 0.1 || m.mfact > 0.9)
        goto invalid;
      m.number = (int)number;
      if (monitor_by_number(&parsed, m.number))
        goto invalid;
      m.selected = selected != 0;
      m.x = (int)x;
      m.y = (int)y;
      m.width = (int)width;
      m.height = (int)height;
      m.work_x = (int)work_x;
      m.work_y = (int)work_y;
      m.work_width = (int)work_width;
      m.work_height = (int)work_height;
      m.nmaster = (int)nmaster;
      m.layout = dupbytes(fields[14], strlen(fields[14]));
      m.visible_count = UINT_MAX;
      if (!m.layout) goto oom;
      monitors = realloc(parsed.monitors,
                         (parsed.monitor_count + 1u) * sizeof(*monitors));
      if (!monitors) { free(m.layout); goto oom; }
      parsed.monitors = monitors;
      parsed.monitors[parsed.monitor_count++] = m;
      continue;
    }
    if (!strncmp(line, "stack\t", 6)) {
      char *fields[4];
      unsigned long mon_value, focused_value, visible_value;
      RawpanelRawmMonitor *m;

      if (split_fields(line, fields, 4u) < 0 || strcmp(fields[0], "stack") ||
          parse_ulong_field(fields[1], 10, INT_MAX, &mon_value) < 0 ||
          parse_ulong_field(fields[2], 10, UINT_MAX, &focused_value) < 0 ||
          parse_ulong_field(fields[3], 10, UINT_MAX, &visible_value) < 0 ||
          !(m = monitor_by_number(&parsed, (int)mon_value)) ||
          m->visible_count != UINT_MAX || focused_value > visible_value)
        goto invalid;
      m->focused_index = (unsigned int)focused_value;
      m->visible_count = (unsigned int)visible_value;
      continue;
    }
    if (!strncmp(line, "tag\t", 4)) {
      char *fields[4];
      unsigned long mon_value, index_value;
      RawpanelRawmMonitor *m;

      if (split_fields(line, fields, 4u) < 0 || strcmp(fields[0], "tag") ||
          parse_ulong_field(fields[1], 10, INT_MAX, &mon_value) < 0 ||
          parse_ulong_field(fields[2], 10, parsed.tag_count, &index_value) < 0 ||
          index_value == 0 ||
          !(m = monitor_by_number(&parsed, (int)mon_value)) ||
          m->tags[index_value - 1u])
        goto invalid;
      m->tags[index_value - 1u] = unescape(fields[3]);
      if (!m->tags[index_value - 1u]) goto invalid;
      continue;
    }
    goto invalid;
  }
  free(text);
  text = NULL;
  if (!header || !tag_count_seen || !selected_monitor_seen ||
      !parsed.tag_count || !parsed.monitor_count)
    goto invalid_no_text;
  {
    size_t i, j;
    unsigned int selected_count = 0;
    for (i = 0; i < parsed.monitor_count; ++i) {
      RawpanelRawmMonitor *m = &parsed.monitors[i];
      if (m->selected) ++selected_count;
      if (m->visible_count == UINT_MAX) goto invalid_no_text;
      for (j = 0; j < parsed.tag_count; ++j)
        if (!m->tags[j]) goto invalid_no_text;
    }
    if (parsed.selected_monitor < 0 || selected_count != 1)
      goto invalid_no_text;
    {
      RawpanelRawmMonitor *selected = monitor_by_number(&parsed, parsed.selected_monitor);
      if (!selected || !selected->selected) goto invalid_no_text;
    }
  }
  if (read_client_titles(rawm, &parsed, err) < 0)
    goto invalid_no_text;
  {
    size_t i;
    for (i = 0; i < parsed.monitor_count; ++i)
      if (!parsed.monitors[i].title) {
        parsed.monitors[i].title = strdup("");
        if (!parsed.monitors[i].title) goto oom;
      }
  }
  rawpanel_rawm_state_destroy(state);
  *state = parsed;
  return 0;

oom:
  if (err) fputs("rawpanel: out of memory parsing rawm state\n", err);
invalid:
  free(text);
invalid_no_text:
  rawpanel_rawm_state_destroy(&parsed);
  if (err) fputs("rawpanel: invalid rawm authoritative state\n", err);
  return -1;
}

static int
read_client_titles(RawpanelRawm *rawm, RawpanelRawmState *state, FILE *err)
{
  unsigned char *property = NULL;
  unsigned long length = 0;
  char *text = NULL, *save = NULL, *line;
  bool header = false;

  if (read_property(rawm, rawm->clients, &property, &length) < 0)
    goto invalid;
  if (length && memchr(property, '\0', length)) {
    XFree(property);
    goto invalid;
  }
  text = dupbytes((const char *)property, (size_t)length);
  XFree(property);
  property = NULL;
  if (!text)
    goto oom;

  for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    char *fields[8];
    size_t count = 0;
    char *p = line;
    unsigned long xid_value, monitor_value, tags_value;
    char *class_name = NULL, *title = NULL;
    size_t i;

    if (!header) {
      if (strcmp(line, "rawm-clients-v1")) goto invalid;
      header = true;
      continue;
    }
    if (!*line) goto invalid;
    fields[count++] = p;
    while (*p) {
      if (*p == '\t') {
        *p = '\0';
        if (count >= sizeof(fields) / sizeof(fields[0])) goto invalid;
        fields[count++] = p + 1;
      }
      ++p;
    }
    if (count != 8 || strcmp(fields[0], "client") ||
        (strcmp(fields[4], "0") && strcmp(fields[4], "1")) ||
        (strcmp(fields[5], "0") && strcmp(fields[5], "1")) ||
        !escaped_text_valid(fields[6]) || !escaped_text_valid(fields[7]))
      goto invalid;

    {
      Window xid;
      uint32_t tags;
      if (parse_xid_field(fields[1], false, &xid) < 0 ||
          parse_ulong_field(fields[2], 10, INT_MAX, &monitor_value) < 0 ||
          parse_mask_field(fields[3], &tags) < 0)
        goto invalid;
      xid_value = (unsigned long)xid;
      tags_value = (unsigned long)tags;
    }
    (void)monitor_value;
    (void)tags_value;

    class_name = unescape(fields[6]);
    title = unescape(fields[7]);
    if (!class_name || !title) {
      free(class_name); free(title); goto invalid;
    }
    free(class_name);
    for (i = 0; i < state->monitor_count; ++i) {
      RawpanelRawmMonitor *m = &state->monitors[i];
      if ((unsigned long)m->focused == xid_value) {
        if (m->title) { free(title); goto invalid; }
        m->title = title;
        title = NULL;
        break;
      }
    }
    free(title);
  }
  free(text);
  if (!header) goto invalid_no_text;
  return 0;

oom:
  if (err) fputs("rawpanel: out of memory parsing rawm clients\n", err);
  free(text);
  return -1;
invalid:
  if (property) XFree(property);
  free(text);
invalid_no_text:
  if (err) fputs("rawpanel: invalid rawm authoritative client inventory\n", err);
  return -1;
}

static int command_atom_name(const char *name, char *buffer, size_t capacity,
                             int32_t *arg0, int32_t *arg1)
{
  const char *suffix = NULL;
  int layout;
  *arg0 = 0; *arg1 = 0;
  if (!strcmp(name, "focus_stack")) suffix = "FOCUS_STACK";
  else if (!strcmp(name, "zoom")) suffix = "ZOOM";
  else if (!strcmp(name, "view")) suffix = "VIEW";
  else if (!strcmp(name, "toggle_view")) suffix = "TOGGLE_VIEW";
  else if (!strcmp(name, "cycle_view")) suffix = "CYCLE_VIEW";
  else if (!strcmp(name, "set_layout")) suffix = "SET_LAYOUT";
  else if (!strcmp(name, "cycle_layout")) suffix = "CYCLE_LAYOUT";
  else if (!strcmp(name, "toggle_floating")) suffix = "TOGGLE_FLOATING";
  else if (!strcmp(name, "tag")) suffix = "TAG";
  else if (!strcmp(name, "toggle_tag")) suffix = "TOGGLE_TAG";
  else return -1;
  if (snprintf(buffer, capacity, "_RAWM_CMD_%s", suffix) < 0) return -1;
  (void)layout;
  return 0;
}

int rawpanel_rawm_layout_id(const char *name)
{
  return layout_id(name);
}

static int
command_supported(RawpanelRawm *rawm, Atom opcode)
{
  Atom type = None;
  int format = 0;
  unsigned long count = 0, after = 0, i;
  unsigned char *data = NULL;
  int found = 0;

  if (XGetWindowProperty(rawm->display, rawm->owner, rawm->supported,
                         0, 256, False, XA_ATOM, &type, &format,
                         &count, &after, &data) != Success ||
      type != XA_ATOM || format != 32 || after != 0) {
    if (data) XFree(data);
    return -1;
  }
  for (i = 0; i < count; ++i)
    if (((Atom *)data)[i] == opcode) { found = 1; break; }
  if (data) XFree(data);
  return found;
}

int rawpanel_rawm_send(RawpanelRawm *rawm, const char *name,
                       int32_t arg0, int32_t arg1, FILE *err)
{
  char atom_name[64];
  Atom opcode;
  XEvent ev;
  int32_t ignored0, ignored1;
  if (!rawm || !rawm->owner || !name)
    return -1;
  if (command_atom_name(name, atom_name, sizeof(atom_name), &ignored0, &ignored1) < 0) {
    if (err) fprintf(err, "rawpanel: unsupported rawm command '%s'\n", name);
    return -1;
  }
  opcode = XInternAtom(rawm->display, atom_name, False);
  if (!opcode) return -1;
  {
    int supported = command_supported(rawm, opcode);
    if (supported <= 0) {
      if (err)
        fprintf(err, supported == 0
                ? "rawpanel: rawm does not advertise command '%s'\n"
                : "rawpanel: cannot read rawm command capabilities for '%s'\n",
                name);
      return -1;
    }
  }
  memset(&ev, 0, sizeof(ev));
  ev.xclient.type = ClientMessage;
  ev.xclient.display = rawm->display;
  ev.xclient.window = rawm->owner;
  ev.xclient.message_type = rawm->command;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = RAWM_CONTROL_VERSION;
  ev.xclient.data.l[1] = (long)opcode;
  ev.xclient.data.l[2] = (long)(uint32_t)arg0;
  ev.xclient.data.l[3] = (long)(uint32_t)arg1;
  ev.xclient.data.l[4] = (long)(uint32_t)rawm->requestor;
  if (!XSendEvent(rawm->display, rawm->owner, False, NoEventMask, &ev)) {
    if (err) fputs("rawpanel: could not send rawm command\n", err);
    return -1;
  }
  XFlush(rawm->display);
  return 0;
}

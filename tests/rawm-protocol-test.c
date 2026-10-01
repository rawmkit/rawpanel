#include "rawm.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "rawpanel rawm protocol test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

static Window
make_owner(Display *dpy, Window root)
{
  XSetWindowAttributes attrs;

  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  return XCreateWindow(dpy, root, -1, -1, 1, 1, 0, 0, InputOnly,
                       CopyFromParent, CWOverrideRedirect, &attrs);
}

static void
set_cardinal(Display *dpy, Window win, const char *name, unsigned long value)
{
  Atom prop = XInternAtom(dpy, name, False);

  XChangeProperty(dpy, win, prop, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&value, 1);
}

static void
set_utf8(Display *dpy, Window win, const char *name, const char *value)
{
  Atom prop = XInternAtom(dpy, name, False);
  Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);

  XChangeProperty(dpy, win, prop, utf8, 8, PropModeReplace,
                  (const unsigned char *)value, (int)strlen(value));
}

static void
set_supported(Display *dpy, Window win, const char *const *names, size_t count)
{
  Atom prop = XInternAtom(dpy, "_RAWM_SUPPORTED_COMMANDS", False);
  Atom *atoms = NULL;
  size_t i;

  if (count) {
    atoms = calloc(count, sizeof(*atoms));
    if (!atoms)
      fail("out of memory");
    for (i = 0; i < count; ++i)
      atoms[i] = XInternAtom(dpy, names[i], False);
  }
  XChangeProperty(dpy, win, prop, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)atoms, (int)count);
  free(atoms);
}

int
main(void)
{
  static const char state[] =
    "rawm-state-v2\n"
    "tag-count\t2\n"
    "selected-monitor\t0\n"
    "monitor\t0\t1\t0\t0\t800\t600\t0\t0\t800\t600\t00000001\t00000003\t00000000\ttile\t0x101\t1\t0.550000\n"
    "stack\t0\t1\t2\n"
    "tag\t0\t1\tone\n"
    "tag\t0\t2\ttwo\n";
  static const char clients[] =
    "rawm-clients-v1\n"
    "client\t0x101\t0\t00000001\t1\t1\tAlphaClass\t%E2%9A%A1 Alpha%25desk\n"
    "client\t0x202\t0\t00000002\t0\t0\tBetaClass\tBeta desk\n";
  static const char *const supported[] = { "_RAWM_CMD_VIEW" };
  Display *dpy = XOpenDisplay(NULL);
  int screen;
  Window root, owner;
  char selection_name[64];
  Atom selection;
  RawpanelRawm rawm;
  RawpanelRawmState parsed = {0};

  if (!dpy)
    return 77;
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  owner = make_owner(dpy, root);
  if (!owner)
    fail("could not create authority owner");
  snprintf(selection_name, sizeof(selection_name), "_RAWM_CONTROL_S%d", screen);
  selection = XInternAtom(dpy, selection_name, False);
  XSetSelectionOwner(dpy, selection, owner, CurrentTime);
  set_cardinal(dpy, owner, "_RAWM_CONTROL_VERSION", 1);
  set_supported(dpy, owner, supported, 1);
  set_utf8(dpy, owner, "_RAWM_STATE", state);
  set_utf8(dpy, owner, "_RAWM_CLIENTS", clients);
  XSync(dpy, False);

  if (rawpanel_rawm_init(&rawm, dpy, screen, stderr) < 0 ||
      rawpanel_rawm_discover(&rawm, stderr) != 1)
    fail("could not discover valid rawm authority");
  if (rawpanel_rawm_read_state(&rawm, &parsed, stderr) < 0)
    fail("valid state was rejected");
  if (parsed.monitor_count != 1 || parsed.tag_count != 2 ||
      strcmp(parsed.monitors[0].layout, "tile") ||
      strcmp(parsed.monitors[0].title, "\xe2\x9a\xa1 Alpha%desk"))
    fail("valid state decoded incorrectly");

  set_utf8(dpy, owner, "_RAWM_STATE",
           "rawm-state-v2\nunknown\tvalue\n");
  if (rawpanel_rawm_read_state(&rawm, &parsed, NULL) == 0)
    fail("unknown state record was accepted");

  set_utf8(dpy, owner, "_RAWM_STATE",
           "rawm-state-v2\n"
           "tag-count\t1\n"
           "selected-monitor\t0\n"
           "monitor\t0\t1\t0\t0\t800\t600\t0\t0\t800\t600\t1\t00000001\t00000000\ttile\t0x101\t1\t0.550000\n"
           "stack\t0\t1\t1\n"
           "tag\t0\t1\tone\n");
  if (rawpanel_rawm_read_state(&rawm, &parsed, NULL) == 0)
    fail("short tag mask was accepted");

  set_utf8(dpy, owner, "_RAWM_STATE", state);
  set_utf8(dpy, owner, "_RAWM_CLIENTS",
           "rawm-clients-v1\n"
           "client\t257\t0\t00000001\t1\t1\tAlphaClass\tAlpha\n");
  if (rawpanel_rawm_read_state(&rawm, &parsed, NULL) == 0)
    fail("client XID without 0x prefix was accepted");

  set_utf8(dpy, owner, "_RAWM_CLIENTS", clients);
  if (rawpanel_rawm_send(&rawm, "view", 1, 0, stderr) < 0)
    fail("advertised command was rejected");
  set_supported(dpy, owner, NULL, 0);
  if (rawpanel_rawm_send(&rawm, "view", 1, 0, NULL) == 0)
    fail("unadvertised command was sent");

  rawpanel_rawm_state_destroy(&parsed);
  rawpanel_rawm_destroy(&rawm);
  XDestroyWindow(dpy, owner);
  XCloseDisplay(dpy);
  puts("rawpanel rawm protocol: ok");
  return EXIT_SUCCESS;
}

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static Window parse_window(const char *text)
{
  char *end = NULL;
  unsigned long value;
  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || !end || end == text || *end || !value || value > UINT32_MAX)
    return None;
  return (Window)value;
}

static int window_above(Display *dpy, Window lower, Window upper)
{
  Window root = RootWindow(dpy, DefaultScreen(dpy));
  Window root_return, parent, *children = NULL;
  unsigned int n = 0, i;
  int lower_index = -1, upper_index = -1;
  if (!XQueryTree(dpy, root, &root_return, &parent, &children, &n)) return 0;
  for (i = 0; i < n; ++i) {
    if (children[i] == lower) lower_index = (int)i;
    if (children[i] == upper) upper_index = (int)i;
  }
  if (children) XFree(children);
  return lower_index >= 0 && upper_index > lower_index;
}

static int hold_surface(Display *dpy)
{
  Window root = RootWindow(dpy, DefaultScreen(dpy));
  XSetWindowAttributes attrs = {0};
  Window win;
  attrs.override_redirect = True;
  win = XCreateWindow(dpy, root, 40, 40, 320, 180, 0, CopyFromParent,
                      InputOutput, CopyFromParent, CWOverrideRedirect, &attrs);
  if (!win) return 1;
  XMapRaised(dpy, win);
  XFlush(dpy);
  printf("0x%lx\n", (unsigned long)win);
  fflush(stdout);
  for (;;) pause();
}

static int touch_rawm_state(Display *dpy)
{
  int screen = DefaultScreen(dpy);
  char selection[64];
  Atom sel, prop, actual = None;
  Window owner;
  int format = 0;
  unsigned long n = 0, after = 0;
  unsigned char *data = NULL;

  snprintf(selection, sizeof(selection), "_RAWM_CONTROL_S%d", screen);
  sel = XInternAtom(dpy, selection, True);
  prop = XInternAtom(dpy, "_RAWM_STATE", True);
  if (!sel || !prop || !(owner = XGetSelectionOwner(dpy, sel))) return 1;
  if (XGetWindowProperty(dpy, owner, prop, 0, 65536, False, AnyPropertyType,
                         &actual, &format, &n, &after, &data) != Success ||
      actual == None || (format != 8 && format != 16 && format != 32) || after) {
    if (data) XFree(data);
    return 1;
  }
  XChangeProperty(dpy, owner, prop, actual, format, PropModeReplace, data, (int)n);
  if (data) XFree(data);
  XSync(dpy, False);
  return 0;
}

static int has_dock_type(Display *dpy, Window win, Atom prop, Atom dock)
{
  Atom actual = None; int format = 0; unsigned long n = 0, after = 0;
  unsigned char *data = NULL; int found = 0; unsigned long i;
  if (XGetWindowProperty(dpy, win, prop, 0, 32, False, XA_ATOM,
                         &actual, &format, &n, &after, &data) == Success &&
      actual == XA_ATOM && format == 32) {
    Atom *atoms = (Atom *)data;
    for (i = 0; i < n; ++i) if (atoms[i] == dock) found = 1;
  }
  if (data) XFree(data);
  return found;
}

static Window find_dock(Display *dpy, Window root)
{
  Window root_return, parent, *children = NULL; unsigned int n = 0, i;
  Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
  if (!XQueryTree(dpy, root, &root_return, &parent, &children, &n)) return None;
  for (i = 0; i < n; ++i) {
    if (has_dock_type(dpy, children[i], type, dock)) {
      Window w = children[i]; XFree(children); return w;
    }
  }
  if (children) XFree(children);
  return None;
}

static int print_dock(Display *dpy, Window root)
{
  Window w = find_dock(dpy, root);
  XWindowAttributes a;
  Atom strutp = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False), actual = None;
  int format = 0; unsigned long n = 0, after = 0; unsigned char *data = NULL;
  unsigned long top = 0, bottom = 0;
  if (!w || !XGetWindowAttributes(dpy, w, &a)) return 1;
  if (XGetWindowProperty(dpy, w, strutp, 0, 12, False, XA_CARDINAL,
                         &actual, &format, &n, &after, &data) == Success &&
      actual == XA_CARDINAL && format == 32 && n == 12) {
    unsigned long *v = (unsigned long *)data; top = v[2]; bottom = v[3];
  }
  if (data) XFree(data);
  printf("0x%lx %d %d %d %d %d %lu %lu\n", w, a.x, a.y, a.width, a.height,
         a.map_state == IsViewable ? 1 : 0, top, bottom);
  return 0;
}


static int supported(Display *dpy, const char *name)
{
  int screen = DefaultScreen(dpy);
  char selection[64];
  Atom sel, prop, wanted, actual = None;
  Window owner;
  int format = 0;
  unsigned long n = 0, after = 0, i;
  unsigned char *data = NULL;
  int found = 0;

  snprintf(selection, sizeof(selection), "_RAWPANEL_CONTROL_S%d", screen);
  sel = XInternAtom(dpy, selection, True);
  prop = XInternAtom(dpy, "_RAWPANEL_SUPPORTED_COMMANDS", True);
  wanted = XInternAtom(dpy, name, True);
  if (!sel || !prop || !wanted || !(owner = XGetSelectionOwner(dpy, sel)))
    return 1;
  if (XGetWindowProperty(dpy, owner, prop, 0, 64, False, XA_ATOM,
                         &actual, &format, &n, &after, &data) == Success &&
      actual == XA_ATOM && format == 32 && after == 0) {
    Atom *atoms = (Atom *)data;
    for (i = 0; i < n; ++i)
      if (atoms[i] == wanted) { found = 1; break; }
  }
  if (data) XFree(data);
  return found ? 0 : 1;
}

static int toggle(Display *dpy)
{
  int screen = DefaultScreen(dpy); char name[64];
  Atom sel, msg, op; Window owner; XEvent e;
  snprintf(name, sizeof(name), "_RAWPANEL_CONTROL_S%d", screen);
  sel = XInternAtom(dpy, name, True);
  if (!sel || !(owner = XGetSelectionOwner(dpy, sel))) return 1;
  msg = XInternAtom(dpy, "_RAWPANEL_COMMAND", True);
  op = XInternAtom(dpy, "_RAWPANEL_CMD_TOGGLE_VISIBILITY", True);
  if (!msg || !op) return 1;
  memset(&e, 0, sizeof(e));
  e.xclient.type = ClientMessage; e.xclient.window = owner;
  e.xclient.message_type = msg; e.xclient.format = 32;
  e.xclient.data.l[0] = 1; e.xclient.data.l[1] = op;
  if (!XSendEvent(dpy, owner, False, NoEventMask, &e)) return 1;
  XFlush(dpy); return 0;
}

int main(int argc, char **argv)
{
  Display *dpy = XOpenDisplay(NULL); int rc;
  if (!dpy) return 77;
  if (argc == 2 && !strcmp(argv[1], "dock"))
    rc = print_dock(dpy, RootWindow(dpy, DefaultScreen(dpy)));
  else if (argc == 2 && !strcmp(argv[1], "toggle"))
    rc = toggle(dpy);
  else if (argc == 2 && !strcmp(argv[1], "hold"))
    rc = hold_surface(dpy);
  else if (argc == 2 && !strcmp(argv[1], "touch-rawm"))
    rc = touch_rawm_state(dpy);
  else if (argc == 3 && !strcmp(argv[1], "supported"))
    rc = supported(dpy, argv[2]);
  else if (argc == 4 && !strcmp(argv[1], "above")) {
    Window lower = parse_window(argv[2]), upper = parse_window(argv[3]);
    rc = lower && upper && window_above(dpy, lower, upper) ? 0 : 1;
  } else
    rc = 2;
  XCloseDisplay(dpy); return rc;
}

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_MAPPED (1u << 0)

static int trapped_error;

static int trap_xerror(Display *dpy, XErrorEvent *event)
{
  (void)dpy;
  trapped_error = event->error_code;
  return 0;
}

static void sleep_ms(unsigned int milliseconds)
{
  struct timespec delay;
  delay.tv_sec = milliseconds / 1000u;
  delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000l;
  while (nanosleep(&delay, &delay) < 0 && errno == EINTR) ;
}

static Window wait_owner(Display *dpy, Atom selection)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    Window owner = XGetSelectionOwner(dpy, selection);
    if (owner != None) return owner;
    sleep_ms(20); elapsed += 20;
  }
  return None;
}

static int wait_owner_change(Display *dpy, Atom selection, Window owner)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    if (XGetSelectionOwner(dpy, selection) != owner)
      return 0;
    sleep_ms(20); elapsed += 20;
  }
  return -1;
}

static int query_parent(Display *dpy, Window win, Window *parent_out)
{
  int (*old_handler)(Display *, XErrorEvent *);
  Window root, parent, *children = NULL;
  unsigned int n = 0;
  Status status;

  trapped_error = 0;
  old_handler = XSetErrorHandler(trap_xerror);
  status = XQueryTree(dpy, win, &root, &parent, &children, &n);
  XSync(dpy, False);
  XSetErrorHandler(old_handler);
  if (children) XFree(children);
  if (!status || trapped_error != 0)
    return -1;
  *parent_out = parent;
  return 0;
}

static int wait_parent(Display *dpy, Window win, Window root, int embedded)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    Window parent;
    if (query_parent(dpy, win, &parent) == 0 &&
        ((parent != root) == embedded))
      return 0;
    sleep_ms(20); elapsed += 20;
  }
  return -1;
}

static int wait_parent_is(Display *dpy, Window win, Window wanted)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    Window parent;
    if (query_parent(dpy, win, &parent) == 0 && parent == wanted)
      return 0;
    sleep_ms(20); elapsed += 20;
  }
  return -1;
}

static int wait_mapped(Display *dpy, Window win, int mapped)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    XWindowAttributes a;
    if (XGetWindowAttributes(dpy, win, &a) &&
        ((a.map_state == IsViewable) == mapped))
      return 0;
    sleep_ms(20); elapsed += 20;
  }
  return -1;
}

static void set_xembed_info(Display *dpy, Window win, Atom atom,
                            unsigned long flags)
{
  unsigned long info[2] = {0, flags};
  XChangeProperty(dpy, win, atom, atom, 32, PropModeReplace,
                  (unsigned char *)info, 2);
  XFlush(dpy);
}

static int request_dock(Display *dpy, Window owner, Atom opcode, Window icon)
{
  XEvent event;

  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.window = owner;
  event.xclient.message_type = opcode;
  event.xclient.format = 32;
  event.xclient.data.l[0] = CurrentTime;
  event.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
  event.xclient.data.l[2] = (long)icon;
  if (!XSendEvent(dpy, owner, False, NoEventMask, &event))
    return -1;
  XFlush(dpy);
  return 0;
}

static int lifecycle(Display *dpy, Window root, Window owner, Atom opcode,
                     Atom xembed_info)
{
  Window icon;

  icon = XCreateSimpleWindow(dpy, root, 0, 0, 12, 12, 0, 0, 0);
  set_xembed_info(dpy, icon, xembed_info, 0);
  if (request_dock(dpy, owner, opcode, icon) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }
  if (wait_parent(dpy, icon, root, 1) < 0 || wait_mapped(dpy, icon, 0) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }

  set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
  if (wait_mapped(dpy, icon, 1) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }
  set_xembed_info(dpy, icon, xembed_info, 0);
  if (wait_mapped(dpy, icon, 0) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }

  XDestroyWindow(dpy, icon);
  XSync(dpy, False);
  return 0;
}

static int disappearance_race(Display *dpy, Window root, Window owner,
                              Atom opcode, Atom xembed_info)
{
  unsigned int i;

  for (i = 0; i < 32; ++i) {
    Window icon = XCreateSimpleWindow(dpy, root, 0, 0, 12, 12, 0, 0, 0);
    set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
    if (request_dock(dpy, owner, opcode, icon) < 0) {
      XDestroyWindow(dpy, icon); return 1;
    }
    set_xembed_info(dpy, icon, xembed_info, 0);
    XDestroyWindow(dpy, icon);
  }
  XSync(dpy, False);
  return 0;
}

static int survive_manager_loss(Display *dpy, Window root, Window owner,
                                Atom selection, Atom opcode, Atom xembed_info)
{
  Window icon;

  icon = XCreateSimpleWindow(dpy, root, 0, 0, 12, 12, 0, 0, 0);
  set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
  if (request_dock(dpy, owner, opcode, icon) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }
  if (wait_parent(dpy, icon, root, 1) < 0 || wait_mapped(dpy, icon, 1) < 0) {
    XDestroyWindow(dpy, icon); return 1;
  }

  puts("ready");
  fflush(stdout);
  if (wait_owner_change(dpy, selection, owner) < 0)
    return 1;
  if (wait_parent_is(dpy, icon, root) < 0)
    return 1;

  XDestroyWindow(dpy, icon);
  XSync(dpy, False);
  return 0;
}

int main(int argc, char **argv)
{
  Display *dpy = XOpenDisplay(NULL);
  int screen, rc;
  Window root, owner;
  Atom selection, opcode, xembed_info;
  char name[64];
  const char *mode = argc > 1 ? argv[1] : "lifecycle";

  if (argc > 2) return 2;
  if (!dpy) return 77;
  screen = DefaultScreen(dpy); root = RootWindow(dpy, screen);
  snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", screen);
  selection = XInternAtom(dpy, name, False);
  opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
  owner = wait_owner(dpy, selection);
  if (owner == None) { XCloseDisplay(dpy); return 1; }

  if (!strcmp(mode, "lifecycle"))
    rc = lifecycle(dpy, root, owner, opcode, xembed_info);
  else if (!strcmp(mode, "race"))
    rc = disappearance_race(dpy, root, owner, opcode, xembed_info);
  else if (!strcmp(mode, "survive"))
    rc = survive_manager_loss(dpy, root, owner, selection, opcode, xembed_info);
  else
    rc = 2;

  XCloseDisplay(dpy);
  if (rc == 0 && !strcmp(mode, "lifecycle"))
    puts("tray client tests: ok");
  return rc;
}

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_MAPPED (1u << 0)

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

static int wait_parent(Display *dpy, Window win, Window root, int embedded)
{
  int elapsed = 0;
  while (elapsed <= 2000) {
    Window rr, parent, *children = NULL; unsigned int n = 0;
    if (XQueryTree(dpy, win, &rr, &parent, &children, &n)) {
      if (children) XFree(children);
      if ((parent != root) == embedded) return 0;
    }
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

int main(void)
{
  Display *dpy = XOpenDisplay(NULL);
  int screen;
  Window root, owner, icon;
  Atom selection, opcode, xembed_info;
  char name[64];
  XEvent event;

  if (!dpy) return 77;
  screen = DefaultScreen(dpy); root = RootWindow(dpy, screen);
  snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", screen);
  selection = XInternAtom(dpy, name, False);
  opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
  owner = wait_owner(dpy, selection);
  if (owner == None) { XCloseDisplay(dpy); return 1; }

  icon = XCreateSimpleWindow(dpy, root, 0, 0, 12, 12, 0, 0, 0);
  set_xembed_info(dpy, icon, xembed_info, 0);
  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.window = owner;
  event.xclient.message_type = opcode;
  event.xclient.format = 32;
  event.xclient.data.l[0] = CurrentTime;
  event.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
  event.xclient.data.l[2] = (long)icon;
  if (!XSendEvent(dpy, owner, False, NoEventMask, &event)) {
    XDestroyWindow(dpy, icon); XCloseDisplay(dpy); return 1;
  }
  XFlush(dpy);
  if (wait_parent(dpy, icon, root, 1) < 0 || wait_mapped(dpy, icon, 0) < 0) {
    XDestroyWindow(dpy, icon); XCloseDisplay(dpy); return 1;
  }

  set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
  if (wait_mapped(dpy, icon, 1) < 0) {
    XDestroyWindow(dpy, icon); XCloseDisplay(dpy); return 1;
  }
  set_xembed_info(dpy, icon, xembed_info, 0);
  if (wait_mapped(dpy, icon, 0) < 0) {
    XDestroyWindow(dpy, icon); XCloseDisplay(dpy); return 1;
  }

  XDestroyWindow(dpy, icon);
  XCloseDisplay(dpy);
  puts("tray client tests: ok");
  return 0;
}

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t done;
static void stop(int sig) { (void)sig; done = 1; }

int main(void)
{
  Display *dpy = XOpenDisplay(NULL);
  int screen;
  Window root, owner;
  XSetWindowAttributes attrs = {0};
  Atom selection, version_atom, state_atom, utf8;
  unsigned long version = 1;
  char selection_name[64];
  const char state[] =
    "rawm-state-v1\n"
    "tag-count\t3\n"
    "selected-monitor\t0\n"
    "monitor\t0\t1\t0\t0\t800\t600\t00000001\t00000003\t00000000\ttile\t0x0\t1\t0.500000\n"
    "stack\t0\t0\t0\n"
    "tag\t0\t1\tone\n"
    "tag\t0\t2\ttwo\n"
    "tag\t0\t3\tthree\n";

  if (!dpy) return 77;
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  snprintf(selection_name, sizeof(selection_name), "_RAWM_CONTROL_S%d", screen);
  selection = XInternAtom(dpy, selection_name, False);
  version_atom = XInternAtom(dpy, "_RAWM_CONTROL_VERSION", False);
  state_atom = XInternAtom(dpy, "_RAWM_STATE", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  attrs.override_redirect = True;
  owner = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, 0, InputOnly,
                        CopyFromParent, CWOverrideRedirect, &attrs);
  XSetSelectionOwner(dpy, selection, owner, CurrentTime);
  XChangeProperty(dpy, owner, version_atom, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&version, 1);
  XChangeProperty(dpy, owner, state_atom, utf8, 8, PropModeReplace,
                  (const unsigned char *)state, (int)strlen(state));
  XSync(dpy, False);
  if (XGetSelectionOwner(dpy, selection) != owner) return 1;
  printf("0x%lx\n", owner);
  fflush(stdout);
  signal(SIGTERM, stop); signal(SIGINT, stop);
  while (!done) pause();
  XDestroyWindow(dpy, owner);
  XCloseDisplay(dpy);
  return 0;
}

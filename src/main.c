/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "rawm.h"
#include "status/worker.h"

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "0.1.0"
#endif

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_EMBEDDED_NOTIFY 0
#define XEMBED_VERSION 0
#define XEMBED_MAPPED (1u << 0)

#define MOD_SHIFT   (1u << 0)
#define MOD_CONTROL (1u << 1)
#define MOD_MOD1    (1u << 2)
#define MOD_MOD4    (1u << 3)

#define MAX_HITS (RAWPANEL_MAX_TAGS + 32)

typedef struct {
  int x, w;
  RawpanelTarget target;
  unsigned int tag;
  const char *status_id;
} Hit;

typedef struct Panel Panel;
struct Panel {
  int monitor;
  Window win;
  Pixmap pixmap;
  XftDraw *draw;
  int x, y, w, h;
  Hit hits[MAX_HITS];
  size_t hit_count;
};

typedef struct TrayIcon TrayIcon;
struct TrayIcon {
  Window win;
  bool mapped;
  TrayIcon *next;
};

typedef struct {
  Window owner;
  Atom selection;
  Atom opcode;
  Atom orientation;
  Atom visual;
  Atom manager;
  Atom xembed;
  Atom xembed_info;
  TrayIcon *icons;
} Tray;

static volatile sig_atomic_t terminate_requested;
static Display *dpy;
static int screen_no;
static Window root;
static Atom net_wm_name, net_wm_type, net_wm_type_dock;
static Atom net_wm_strut, net_wm_strut_partial;
static Atom utf8;
static GC gc;
static XftFont *font;
static XftColor colors[RAWPANEL_SCHEME_COUNT][RAWPANEL_COLOR_COUNT];
static RawpanelConfig cfg;
static RawpanelRawm rawm;
static RawpanelRawmState wmstate;
static RawpanelStatusData statusdata;
static RawpanelStatusWorker statusworker = {.fd = -1, .pid = -1};
static Panel *panels;
static size_t panel_count;
static Tray tray;
static bool panel_visible = true;
static Window control_owner;
static Atom control_selection, control_version_atom, control_supported_atom;
static Atom control_command_atom;
static Atom control_commands[4];
static bool control_volume_available;

static void on_signal(int signo) { (void)signo; terminate_requested = 1; }

static void die(const char *msg)
{
  fputs(msg, stderr);
  exit(EXIT_FAILURE);
}

static int text_width(const char *text)
{
  XGlyphInfo ext;
  if (!text || !*text)
    return 0;
  XftTextExtentsUtf8(dpy, font, (const FcChar8 *)text, (int)strlen(text), &ext);
  return (int)ext.xOff;
}

static unsigned int xmods(unsigned int state)
{
  unsigned int out = 0;
  if (state & ShiftMask) out |= MOD_SHIFT;
  if (state & ControlMask) out |= MOD_CONTROL;
  if (state & Mod1Mask) out |= MOD_MOD1;
  if (state & Mod4Mask) out |= MOD_MOD4;
  return out;
}

static XftColor *scheme_color(RawpanelScheme scheme, RawpanelColor component)
{
  return &colors[scheme][component];
}

static void fill(Pixmap pix, int x, int y, int w, int h, RawpanelScheme scheme)
{
  if (w <= 0 || h <= 0) return;
  XSetForeground(dpy, gc, scheme_color(scheme, RAWPANEL_COLOR_BACKGROUND)->pixel);
  XFillRectangle(dpy, pix, gc, x, y, (unsigned int)w, (unsigned int)h);
}

static void draw_text_at(Panel *p, int x, const char *text,
                         RawpanelScheme scheme)
{
  int ascent = font->ascent;
  int descent = font->descent;
  int baseline = (p->h - (ascent + descent)) / 2 + ascent;

  XftDrawStringUtf8(p->draw, scheme_color(scheme, RAWPANEL_COLOR_FOREGROUND),
                    font, x, baseline, (const FcChar8 *)text, (int)strlen(text));
}

static int draw_text(Panel *p, int x, int width, const char *text,
                     RawpanelScheme scheme, bool cell)
{
  int tw = text_width(text);
  int tx = x + (int)cfg.cell_margin;
  int inner = width - (int)(cfg.cell_margin * 2u);
  if (cell) fill(p->pixmap, x, 0, width, p->h, scheme);
  if (inner < 0) inner = 0;
  if (tw < inner) tx = x + (width - tw) / 2;
  draw_text_at(p, tx, text, scheme);
  return width;
}

static void indicator(Panel *p, int x, int width, RawpanelScheme scheme)
{
  unsigned int h = cfg.indicator_size;
  int y;
  if (!h || width <= 0) return;
  if (h > (unsigned int)p->h) h = (unsigned int)p->h;
  y = cfg.top ? p->h - (int)h : 0;
  XSetForeground(dpy, gc, scheme_color(scheme, RAWPANEL_COLOR_BORDER)->pixel);
  XFillRectangle(dpy, p->pixmap, gc, x, y, (unsigned int)width, h);
}

static void add_hit(Panel *p, int x, int w, RawpanelTarget target,
                    unsigned int tag, const char *status_id)
{
  Hit *h;
  if (w <= 0 || p->hit_count >= MAX_HITS) return;
  h = &p->hits[p->hit_count++];
  h->x = x; h->w = w; h->target = target; h->tag = tag; h->status_id = status_id;
}

static RawpanelRawmMonitor *state_monitor(int number)
{
  size_t i;
  for (i = 0; i < wmstate.monitor_count; ++i)
    if (wmstate.monitors[i].number == number) return &wmstate.monitors[i];
  return NULL;
}

static int layout_label(char *buf, size_t cap, const RawpanelRawmMonitor *m)
{
  const char *name = m->layout ? m->layout : "?";
  if (!strcmp(name, "tile") || !strcmp(name, "bstack") ||
      !strcmp(name, "bstack_horizontal"))
    return snprintf(buf, cap, "%s %d", name, m->nmaster);
  if ((!strcmp(name, "monocle") || !strcmp(name, "floating")) && m->visible_count)
    return snprintf(buf, cap, "%s %u/%u", name, m->focused_index, m->visible_count);
  return snprintf(buf, cap, "%s", name);
}

static unsigned int tray_width(void)
{
  TrayIcon *i;
  unsigned int count = 0;
  if (!cfg.systray_enabled) return 0;
  for (i = tray.icons; i; i = i->next) if (i->mapped) ++count;
  if (!count) return 0;
  return count * cfg.systray_icon_size + (count - 1u) * cfg.systray_spacing;
}

static RawpanelScheme status_scheme(RawpanelStatusState state)
{
  if (state == RAWPANEL_STATUS_CRITICAL) return RAWPANEL_SCHEME_ERROR;
  if (state == RAWPANEL_STATUS_WARNING || state == RAWPANEL_STATUS_STALE)
    return RAWPANEL_SCHEME_URGENT;
  return RAWPANEL_SCHEME_NORMAL;
}

static int status_total_width(void)
{
  size_t i;
  int w = 0;
  int sep = cfg.status_separator ? text_width(cfg.status_separator) : 0;
  for (i = 0; i < statusdata.count; ++i) {
    if (i) w += (int)cfg.item_spacing + sep;
    w += text_width(statusdata.segments[i].text) + (int)(cfg.cell_margin * 2u);
  }
  return w;
}

static void draw_panel(Panel *p)
{
  RawpanelRawmMonitor *m = state_monitor(p->monitor);
  int x, right, title_right;
  bool drew = false;
  unsigned int i;
  char layout[96];
  char *title = NULL;
  if (!m || !p->pixmap || !p->draw) return;
  p->hit_count = 0;
  fill(p->pixmap, 0, 0, p->w, p->h, RAWPANEL_SCHEME_NORMAL);
  x = (int)cfg.padding;

  for (i = 0; i < wmstate.tag_count && i < RAWPANEL_MAX_TAGS; ++i) {
    uint32_t mask = 1u << i;
    const char *name = m->tags[i] ? m->tags[i] : "?";
    int w;
    RawpanelScheme scheme;
    if (!cfg.tags_all && !(m->viewed & mask) && !(m->occupied & mask)) continue;
    if (drew) x += (int)cfg.item_spacing;
    w = text_width(name) + (int)(cfg.cell_margin * 2u);
    scheme = (m->viewed & mask) ? RAWPANEL_SCHEME_SELECTED :
             ((m->urgent & mask) ? RAWPANEL_SCHEME_URGENT : RAWPANEL_SCHEME_NORMAL);
    draw_text(p, x, w, name, scheme, scheme != RAWPANEL_SCHEME_NORMAL);
    if (m->viewed & mask || m->urgent & mask) indicator(p, x, w, scheme);
    add_hit(p, x, w, RAWPANEL_TARGET_TAGS, i + 1u, NULL);
    x += w; drew = true;
  }
  if (drew) x += (int)cfg.section_spacing;

  layout_label(layout, sizeof(layout), m);
  {
    int w = text_width(layout) + (int)(cfg.cell_margin * 2u);
    draw_text(p, x, w, layout, RAWPANEL_SCHEME_SELECTED, true);
    add_hit(p, x, w, RAWPANEL_TARGET_LAYOUT, 0, NULL);
    x += w + (int)cfg.section_spacing;
  }

  right = p->w - (int)cfg.padding;
  if (m->selected) {
    unsigned int tw = tray_width();
    if (tw) right -= (int)tw + (int)cfg.section_spacing;
    if (cfg.status_enabled && statusdata.count) {
      int total = status_total_width();
      int sx = right - total;
      size_t si;
      int cursor;
      if (sx < x) sx = x;
      cursor = sx;
      for (si = 0; si < statusdata.count && cursor < right; ++si) {
        RawpanelScheme scheme = status_scheme(statusdata.segments[si].state);
        int w;
        if (si) {
          int before = (int)cfg.item_spacing / 2;
          int after = (int)cfg.item_spacing - before;
          cursor += before;
          if (cfg.status_separator && *cfg.status_separator) {
            int sw = text_width(cfg.status_separator);
            draw_text_at(p, cursor, cfg.status_separator, RAWPANEL_SCHEME_NORMAL);
            cursor += sw;
          }
          cursor += after;
        }
        w = text_width(statusdata.segments[si].text) + (int)(cfg.cell_margin * 2u);
        if (cursor + w > right) w = right - cursor;
        if (w <= 0) break;
        draw_text(p, cursor, w, statusdata.segments[si].text, scheme,
                  scheme != RAWPANEL_SCHEME_NORMAL);
        if (scheme != RAWPANEL_SCHEME_NORMAL) indicator(p, cursor, w, scheme);
        add_hit(p, cursor, w, RAWPANEL_TARGET_STATUS, 0, statusdata.segments[si].id);
        cursor += w;
      }
      title_right = sx - (int)cfg.section_spacing;
    } else title_right = right;
  } else title_right = right;

  if (title_right > x) {
    int w = title_right - x;
    title = m->title ? strdup(m->title) : strdup("");
    draw_text(p, x, w, title ? title : "", RAWPANEL_SCHEME_NORMAL, false);
    if (m->selected && m->focused) indicator(p, x + w/2 - (w > 12 ? 6 : 0), w > 12 ? 12 : w, RAWPANEL_SCHEME_SELECTED);
    add_hit(p, x, w, RAWPANEL_TARGET_TITLE, 0, NULL);
    free(title);
  }
  XCopyArea(dpy, p->pixmap, p->win, gc, 0, 0, (unsigned int)p->w,
            (unsigned int)p->h, 0, 0);
}

static void draw_all(void)
{
  size_t i;
  for (i = 0; i < panel_count; ++i) draw_panel(&panels[i]);
  XFlush(dpy);
}

static Panel *panel_by_window(Window win)
{
  size_t i;
  for (i = 0; i < panel_count; ++i) if (panels[i].win == win) return &panels[i];
  return NULL;
}

static void set_dock_properties(Panel *p, const RawpanelRawmMonitor *m)
{
  Atom type = net_wm_type_dock;
  unsigned long strut[4] = {0,0,0,0};
  unsigned long partial[12] = {0};
  int root_h = DisplayHeight(dpy, screen_no);
  XClassHint hint = {(char *)"rawpanel", (char *)"rawpanel"};
  XChangeProperty(dpy, p->win, net_wm_type, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&type, 1);
  if (cfg.top) {
    unsigned long v = (unsigned long)(m->y + (int)cfg.height);
    strut[2] = v; partial[2] = v;
    partial[8] = (unsigned long)m->x;
    partial[9] = (unsigned long)(m->x + m->width - 1);
  } else {
    unsigned long v = (unsigned long)(root_h - (m->y + m->height - (int)cfg.height));
    strut[3] = v; partial[3] = v;
    partial[10] = (unsigned long)m->x;
    partial[11] = (unsigned long)(m->x + m->width - 1);
  }
  XChangeProperty(dpy, p->win, net_wm_strut, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)strut, 4);
  XChangeProperty(dpy, p->win, net_wm_strut_partial, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)partial, 12);
  XSetClassHint(dpy, p->win, &hint);
  XStoreName(dpy, p->win, "rawpanel");
  XChangeProperty(dpy, p->win, net_wm_name, utf8, 8, PropModeReplace,
                  (unsigned char *)"rawpanel", 8);
}

static void tray_detach_icons(void)
{
  TrayIcon *icon;

  for (icon = tray.icons; icon; icon = icon->next) {
    XUnmapWindow(dpy, icon->win);
    XReparentWindow(dpy, icon->win, root, 0, 0);
  }
}

static void destroy_panels(void)
{
  size_t i;

  tray_detach_icons();
  for (i = 0; i < panel_count; ++i) {
    if (panels[i].draw) XftDrawDestroy(panels[i].draw);
    if (panels[i].pixmap) XFreePixmap(dpy, panels[i].pixmap);
    if (panels[i].win) XDestroyWindow(dpy, panels[i].win);
  }
  free(panels); panels = NULL; panel_count = 0;
}

static void panel_surface_recreate(Panel *p)
{
  if (p->draw) {
    XftDrawDestroy(p->draw);
    p->draw = NULL;
  }
  if (p->pixmap) {
    XFreePixmap(dpy, p->pixmap);
    p->pixmap = None;
  }
  p->pixmap = XCreatePixmap(dpy, p->win, (unsigned int)p->w,
                            (unsigned int)p->h,
                            (unsigned int)DefaultDepth(dpy, screen_no));
  if (p->pixmap)
    p->draw = XftDrawCreate(dpy, p->pixmap, DefaultVisual(dpy, screen_no),
                            DefaultColormap(dpy, screen_no));
  if (!p->pixmap || !p->draw)
    die("rawpanel: could not create panel drawing surface\n");
}

static void panel_create(Panel *p, RawpanelRawmMonitor *m)
{
  XSetWindowAttributes attrs;
  memset(p, 0, sizeof(*p));
  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  attrs.background_pixel =
      scheme_color(RAWPANEL_SCHEME_NORMAL, RAWPANEL_COLOR_BACKGROUND)->pixel;
  attrs.event_mask = ExposureMask | ButtonPressMask |
                     SubstructureRedirectMask | SubstructureNotifyMask;
  p->monitor = m->number;
  p->x = m->x;
  p->w = m->width;
  p->h = (int)cfg.height;
  p->y = cfg.top ? m->y : m->y + m->height - p->h;
  p->win = XCreateWindow(dpy, root, p->x, p->y, (unsigned int)p->w,
                         (unsigned int)p->h, 0,
                         DefaultDepth(dpy, screen_no), InputOutput,
                         DefaultVisual(dpy, screen_no),
                         CWOverrideRedirect | CWBackPixel | CWEventMask,
                         &attrs);
  if (!p->win)
    die("rawpanel: could not create panel window\n");
  set_dock_properties(p, m);
  panel_surface_recreate(p);
  if (panel_visible)
    XMapRaised(dpy, p->win);
}

static bool panel_topology_matches(void)
{
  size_t i;
  if (panel_count != wmstate.monitor_count)
    return false;
  for (i = 0; i < panel_count; ++i)
    if (panels[i].monitor != wmstate.monitors[i].number)
      return false;
  return true;
}

static void reconcile_panels(void)
{
  size_t i;

  if (!panel_topology_matches()) {
    destroy_panels();
    panels = calloc(wmstate.monitor_count, sizeof(*panels));
    if (!panels)
      die("rawpanel: out of memory\n");
    panel_count = wmstate.monitor_count;
    for (i = 0; i < panel_count; ++i)
      panel_create(&panels[i], &wmstate.monitors[i]);
  } else {
    for (i = 0; i < panel_count; ++i) {
      RawpanelRawmMonitor *m = &wmstate.monitors[i];
      Panel *p = &panels[i];
      int x = m->x;
      int w = m->width;
      int h = (int)cfg.height;
      int y = cfg.top ? m->y : m->y + m->height - h;
      bool resized = p->w != w || p->h != h;

      if (p->x != x || p->y != y || resized) {
        p->x = x;
        p->y = y;
        p->w = w;
        p->h = h;
        XMoveResizeWindow(dpy, p->win, p->x, p->y,
                          (unsigned int)p->w, (unsigned int)p->h);
        if (resized)
          panel_surface_recreate(p);
      }
      set_dock_properties(p, m);
      if (panel_visible)
        XMapRaised(dpy, p->win);
    }
  }
  draw_all();
}

static void tray_send(Window win, long message, long d1, long d2, long d3)
{
  XEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.xclient.type = ClientMessage;
  ev.xclient.window = win;
  ev.xclient.message_type = tray.xembed;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = CurrentTime;
  ev.xclient.data.l[1] = message;
  ev.xclient.data.l[2] = d1;
  ev.xclient.data.l[3] = d2;
  ev.xclient.data.l[4] = d3;
  XSendEvent(dpy, win, False, NoEventMask, &ev);
}

static Panel *selected_panel(void)
{
  size_t i;
  for (i = 0; i < panel_count; ++i) if (panels[i].monitor == wmstate.selected_monitor) return &panels[i];
  return panel_count ? &panels[0] : NULL;
}

static void tray_arrange(void)
{
  TrayIcon *i;
  Panel *p = selected_panel();
  int x;
  if (!p || !cfg.systray_enabled) return;
  x = p->w - (int)cfg.padding - (int)tray_width();
  for (i = tray.icons; i; i = i->next) {
    int y = (p->h - (int)cfg.systray_icon_size) / 2;
    XReparentWindow(dpy, i->win, p->win, x, y);
    XMoveResizeWindow(dpy, i->win, x, y, cfg.systray_icon_size,
                      cfg.systray_icon_size);
    if (i->mapped) {
      XMapRaised(dpy, i->win);
      x += (int)cfg.systray_icon_size + (int)cfg.systray_spacing;
    } else {
      XUnmapWindow(dpy, i->win);
    }
  }
  draw_all();
}

static TrayIcon *tray_find(Window win)
{
  TrayIcon *icon;

  for (icon = tray.icons; icon; icon = icon->next)
    if (icon->win == win)
      return icon;
  return NULL;
}

static void tray_sync_mapped(Window win)
{
  TrayIcon *icon = tray_find(win);
  Atom actual = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;
  bool mapped = false;

  if (!icon)
    return;
  if (XGetWindowProperty(dpy, win, tray.xembed_info, 0, 2, False,
                         tray.xembed_info, &actual, &format, &nitems, &after,
                         &data) == Success && actual == tray.xembed_info &&
      format == 32 && nitems >= 2)
    mapped = (((unsigned long *)data)[1] & XEMBED_MAPPED) != 0;
  if (data)
    XFree(data);

  if (icon->mapped == mapped)
    return;
  icon->mapped = mapped;
  if (mapped)
    XMapRaised(dpy, icon->win);
  else
    XUnmapWindow(dpy, icon->win);
  tray_arrange();
}

static void tray_remove(Window win)
{
  TrayIcon **pp = &tray.icons;
  while (*pp) {
    if ((*pp)->win == win) {
      TrayIcon *old = *pp;
      *pp = old->next;
      free(old);
      tray_arrange();
      return;
    }
    pp = &(*pp)->next;
  }
}

static void tray_dock(Window win)
{
  TrayIcon *icon;
  unsigned long flags = XEMBED_MAPPED;
  Atom actual = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;
  if (!win) return;
  if (tray_find(win)) return;
  icon = calloc(1, sizeof(*icon));
  if (!icon) return;
  icon->win = win; icon->mapped = true; icon->next = tray.icons; tray.icons = icon;
  XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
  if (XGetWindowProperty(dpy, win, tray.xembed_info, 0, 2, False, tray.xembed_info,
                         &actual, &format, &nitems, &after, &data) == Success &&
      actual == tray.xembed_info && format == 32 && nitems >= 2)
    flags = ((unsigned long *)data)[1];
  if (data) XFree(data);
  icon->mapped = (flags & XEMBED_MAPPED) != 0;
  tray_arrange();
  tray_send(win, XEMBED_EMBEDDED_NOTIFY, 0,
            selected_panel() ? (long)selected_panel()->win : 0, XEMBED_VERSION);
}

static void tray_init(void)
{
  char name[64];
  XSetWindowAttributes attrs;
  XEvent ev;
  unsigned long orientation = 0;
  if (!cfg.systray_enabled) return;
  snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", screen_no);
  tray.selection = XInternAtom(dpy, name, False);
  tray.opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  tray.orientation = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
  tray.visual = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
  tray.manager = XInternAtom(dpy, "MANAGER", False);
  tray.xembed = XInternAtom(dpy, "_XEMBED", False);
  tray.xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
  memset(&attrs, 0, sizeof(attrs)); attrs.override_redirect = True;
  tray.owner = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, 0, InputOnly,
                             CopyFromParent, CWOverrideRedirect, &attrs);
  XChangeProperty(dpy, tray.owner, tray.orientation, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&orientation, 1);
  {
    unsigned long visual =
      (unsigned long)XVisualIDFromVisual(DefaultVisual(dpy, screen_no));
    XChangeProperty(dpy, tray.owner, tray.visual, XA_VISUALID, 32,
                    PropModeReplace, (unsigned char *)&visual, 1);
  }
  XSetSelectionOwner(dpy, tray.selection, tray.owner, CurrentTime);
  XSync(dpy, False);
  if (XGetSelectionOwner(dpy, tray.selection) != tray.owner) {
    fputs("rawpanel: another system tray already owns this screen; tray disabled\n", stderr);
    XDestroyWindow(dpy, tray.owner); tray.owner = None; cfg.systray_enabled = false; return;
  }
  memset(&ev, 0, sizeof(ev));
  ev.xclient.type = ClientMessage; ev.xclient.window = root;
  ev.xclient.message_type = tray.manager; ev.xclient.format = 32;
  ev.xclient.data.l[0] = CurrentTime; ev.xclient.data.l[1] = tray.selection;
  ev.xclient.data.l[2] = tray.owner;
  XSendEvent(dpy, root, False, StructureNotifyMask, &ev);
  XFlush(dpy);
}

static void tray_destroy(void)
{
  TrayIcon *i = tray.icons;
  while (i) { TrayIcon *n = i->next; XReparentWindow(dpy, i->win, root, 0, 0); free(i); i = n; }
  if (tray.owner) XDestroyWindow(dpy, tray.owner);
  memset(&tray, 0, sizeof(tray));
}

static int spawn_argv(char *const *argv)
{
  pid_t pid;
  if (!argv || !argv[0]) return -1;
  pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (dpy) close(ConnectionNumber(dpy));
    setsid();
    execvp(argv[0], argv);
    _exit(127);
  }
  return 0;
}

static int dispatch_rawm(const RawpanelButton *b, const Hit *hit)
{
  int32_t arg0 = 0, arg1 = 0;
  if (!b->command) return -1;
  if (!strcmp(b->command, "view") || !strcmp(b->command, "toggle_view") ||
      !strcmp(b->command, "tag") || !strcmp(b->command, "toggle_tag")) {
    if (hit && hit->tag) arg0 = (int32_t)hit->tag;
    else if (b->has_tag) arg0 = (int32_t)b->tag;
    else return -1;
  } else if (!strcmp(b->command, "focus_stack") || !strcmp(b->command, "cycle_view") ||
             !strcmp(b->command, "cycle_layout")) {
    if (!b->has_number) return -1;
    arg0 = b->number;
  } else if (!strcmp(b->command, "set_layout")) {
    arg0 = b->layout ? rawpanel_rawm_layout_id(b->layout) : -1;
    if (arg0 < -1) return -1;
  }
  return rawpanel_rawm_send(&rawm, b->command, arg0, arg1, stderr);
}

static void handle_button(XButtonEvent *ev)
{
  Panel *p = panel_by_window(ev->window);
  Hit *hit = NULL;
  size_t i;
  unsigned int mods = xmods(ev->state);
  if (!p) return;
  for (i = 0; i < p->hit_count; ++i)
    if (ev->x >= p->hits[i].x && ev->x < p->hits[i].x + p->hits[i].w) { hit = &p->hits[i]; break; }
  if (!hit) return;
  for (i = 0; i < cfg.button_count; ++i) {
    RawpanelButton *b = &cfg.buttons[i];
    if (b->target != hit->target || b->button != ev->button || b->modifiers != mods) continue;
    if (b->status_id && (!hit->status_id || strcmp(b->status_id, hit->status_id))) continue;
    switch (b->type) {
      case RAWPANEL_ACTION_RAWM: (void)dispatch_rawm(b, hit); break;
      case RAWPANEL_ACTION_VOLUME_UP:
        (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_UP, stderr); break;
      case RAWPANEL_ACTION_VOLUME_DOWN:
        (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_DOWN, stderr); break;
      case RAWPANEL_ACTION_VOLUME_MUTE:
        (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_MUTE, stderr); break;
      case RAWPANEL_ACTION_EXEC: (void)spawn_argv(b->argv); break;
      default: break;
    }
    return;
  }
}

static void update_wm_state(void)
{
  RawpanelRawmState next = {0};
  if (rawpanel_rawm_read_state(&rawm, &next, stderr) < 0) return;
  rawpanel_rawm_state_destroy(&wmstate);
  wmstate = next;
  reconcile_panels();
  tray_arrange();
}

static bool handle_control_message(XClientMessageEvent *ev);

static void handle_xevent(XEvent *e)
{
  Panel *p;
  if (e->type == Expose && (p = panel_by_window(e->xexpose.window))) {
    if (e->xexpose.count == 0) draw_panel(p);
    return;
  }
  if (e->type == ButtonPress) { handle_button(&e->xbutton); return; }
  if (e->type == PropertyNotify) {
    if (e->xproperty.window == rawm.owner &&
        (e->xproperty.atom == rawm.state || e->xproperty.atom == rawm.clients)) {
      update_wm_state(); return;
    }
    if (e->xproperty.atom == tray.xembed_info && tray_find(e->xproperty.window)) {
      tray_sync_mapped(e->xproperty.window); return;
    }
  }
  if (e->type == DestroyNotify) {
    if (e->xdestroywindow.window == rawm.owner) { terminate_requested = 1; return; }
    tray_remove(e->xdestroywindow.window); return;
  }
  if (e->type == ClientMessage && handle_control_message(&e->xclient)) return;
  if (e->type == ClientMessage && cfg.systray_enabled && tray.owner &&
      e->xclient.window == tray.owner && e->xclient.message_type == tray.opcode &&
      e->xclient.format == 32 && e->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
    tray_dock((Window)e->xclient.data.l[2]); return;
  }
  if (e->type == MapRequest && tray_find(e->xmaprequest.window)) {
    tray_sync_mapped(e->xmaprequest.window);
    return;
  }
  if (e->type == ConfigureRequest && tray_find(e->xconfigurerequest.window)) {
    tray_arrange();
    return;
  }
  if (e->type == ResizeRequest) {
    TrayIcon *i;
    for (i = tray.icons; i; i = i->next) if (i->win == e->xresizerequest.window) { tray_arrange(); return; }
  }
}

static void set_panel_visible(bool visible)
{
  size_t i;
  if (panel_visible == visible)
    return;
  panel_visible = visible;
  for (i = 0; i < panel_count; ++i) {
    if (visible)
      XMapRaised(dpy, panels[i].win);
    else
      XUnmapWindow(dpy, panels[i].win);
  }
  if (visible) {
    tray_arrange();
    draw_all();
  }
  XFlush(dpy);
}

static void control_publish_capabilities(bool volume_control)
{
  Atom supported[4];
  int count = 0;

  if (!control_owner)
    return;
  control_volume_available = volume_control;
  supported[count++] = control_commands[0];
  if (volume_control) {
    supported[count++] = control_commands[1];
    supported[count++] = control_commands[2];
    supported[count++] = control_commands[3];
  }
  XChangeProperty(dpy, control_owner, control_supported_atom, XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)supported, count);
  XFlush(dpy);
}

static int control_init(void)
{
  char name[64];
  unsigned long version = 1;
  XSetWindowAttributes attrs;
  size_t i;
  static const char *const names[4] = {
    "_RAWPANEL_CMD_TOGGLE_VISIBILITY",
    "_RAWPANEL_CMD_VOLUME_UP",
    "_RAWPANEL_CMD_VOLUME_DOWN",
    "_RAWPANEL_CMD_VOLUME_MUTE",
  };
  snprintf(name, sizeof(name), "_RAWPANEL_CONTROL_S%d", screen_no);
  control_selection = XInternAtom(dpy, name, False);
  control_version_atom = XInternAtom(dpy, "_RAWPANEL_CONTROL_VERSION", False);
  control_supported_atom = XInternAtom(dpy, "_RAWPANEL_SUPPORTED_COMMANDS", False);
  control_command_atom = XInternAtom(dpy, "_RAWPANEL_COMMAND", False);
  for (i = 0; i < 4; ++i)
    control_commands[i] = XInternAtom(dpy, names[i], False);
  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  control_owner = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, 0, InputOnly,
                                CopyFromParent, CWOverrideRedirect, &attrs);
  if (!control_owner)
    return -1;
  XSetSelectionOwner(dpy, control_selection, control_owner, CurrentTime);
  XSync(dpy, False);
  if (XGetSelectionOwner(dpy, control_selection) != control_owner)
    return -1;
  XChangeProperty(dpy, control_owner, control_version_atom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&version, 1);
  control_publish_capabilities(false);
  return 0;
}

static bool handle_control_message(XClientMessageEvent *ev)
{
  Atom opcode;
  if (!control_owner || ev->window != control_owner ||
      ev->message_type != control_command_atom || ev->format != 32)
    return false;
  if ((unsigned long)ev->data.l[0] != 1ul)
    return true;
  opcode = (Atom)(unsigned long)ev->data.l[1];
  if (opcode == control_commands[0])
    set_panel_visible(!panel_visible);
  else if (control_volume_available && opcode == control_commands[1])
    (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_UP, stderr);
  else if (control_volume_available && opcode == control_commands[2])
    (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_DOWN, stderr);
  else if (control_volume_available && opcode == control_commands[3])
    (void)rawpanel_status_worker_control(&statusworker, RAWPANEL_STATUS_CONTROL_VOLUME_MUTE, stderr);
  return true;
}

static int init_x(void)
{
  size_t i, j;
  dpy = XOpenDisplay(NULL);
  if (!dpy) return -1;
  screen_no = DefaultScreen(dpy); root = RootWindow(dpy, screen_no);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
  net_wm_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  net_wm_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
  net_wm_strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
  net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
  gc = XCreateGC(dpy, root, 0, NULL);
  font = XftFontOpenName(dpy, screen_no, cfg.font);
  if (!gc || !font) return -1;
  for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
    for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
      if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen_no),
                             DefaultColormap(dpy, screen_no), cfg.colors[i][j],
                             &colors[i][j]))
        return -1;
  return 0;
}

static void cleanup(void)
{
  size_t i, j;
  rawpanel_status_worker_stop(&statusworker, stderr);
  rawpanel_status_data_destroy(&statusdata);
  tray_destroy();
  destroy_panels();
  rawpanel_rawm_state_destroy(&wmstate);
  rawpanel_rawm_destroy(&rawm);
  if (dpy && control_owner) XDestroyWindow(dpy, control_owner);
  control_owner = None;
  if (dpy) {
    if (font) XftFontClose(dpy, font);
    for (i = 0; i < RAWPANEL_SCHEME_COUNT; ++i)
      for (j = 0; j < RAWPANEL_COLOR_COUNT; ++j)
        if (colors[i][j].pixel) XftColorFree(dpy, DefaultVisual(dpy, screen_no),
                                              DefaultColormap(dpy, screen_no), &colors[i][j]);
    if (gc) XFreeGC(dpy, gc);
    XCloseDisplay(dpy);
  }
  rawpanel_config_destroy(&cfg);
}

static void usage(FILE *out)
{
  fprintf(out, "usage: rawpanel [-c FILE] [-C] [--status-debug] [-v]\n");
}

int main(int argc, char **argv)
{
  const char *path = NULL;
  bool check = false;
  bool status_debug = false;
  int opt, rc;
  static const struct option long_options[] = {
    {"status-debug", no_argument, NULL, 1000},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };
  struct sigaction sa;

  rawpanel_config_init(&cfg);
  while ((opt = getopt_long(argc, argv, "c:Cvh", long_options, NULL)) != -1) {
    switch (opt) {
      case 'c': path = optarg; break;
      case 'C': check = true; break;
      case 'v': puts(VERSION); rawpanel_config_destroy(&cfg); return 0;
      case 'h': usage(stdout); rawpanel_config_destroy(&cfg); return 0;
      case 1000: status_debug = true; break;
      default: usage(stderr); rawpanel_config_destroy(&cfg); return 2;
    }
  }
  if (optind != argc) { usage(stderr); rawpanel_config_destroy(&cfg); return 2; }
  if (rawpanel_config_load(&cfg, path, stderr) < 0) { rawpanel_config_destroy(&cfg); return 1; }
  if (check) { rawpanel_config_destroy(&cfg); return 0; }
  if (status_debug) {
    int debug_rc = rawpanel_status_worker_debug(&cfg.status, stdout, stderr);
    rawpanel_config_destroy(&cfg);
    return debug_rc;
  }
  if (init_x() < 0) die("rawpanel: could not initialize Xft/X11\n");
  if (rawpanel_rawm_init(&rawm, dpy, screen_no, stderr) < 0) die("rawpanel: could not initialize rawm client\n");
  if (control_init() < 0) die("rawpanel: could not acquire panel control authority\n");

  memset(&sa, 0, sizeof(sa)); sa.sa_handler = on_signal; sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL);
  signal(SIGCHLD, SIG_IGN);

  while (!terminate_requested) {
    rc = rawpanel_rawm_discover(&rawm, stderr);
    if (rc > 0) break;
    if (rc < 0) { cleanup(); return 1; }
    struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL);
  }
  if (terminate_requested) { cleanup(); return 0; }
  update_wm_state();
  if (!wmstate.monitor_count) die("rawpanel: rawm published no monitors\n");
  tray_init();
  if (cfg.status_enabled) {
    rawpanel_status_worker_init(&statusworker);
    if (rawpanel_status_worker_start(&statusworker, &cfg.status, stderr) < 0)
      fputs("rawpanel: status worker unavailable; continuing without status\n", stderr);
    else if (rawpanel_status_config_has_volume_control(&cfg.status))
      control_publish_capabilities(true);
  }

  while (!terminate_requested) {
    struct pollfd pfds[2];
    nfds_t n = 1;
    pfds[0].fd = ConnectionNumber(dpy); pfds[0].events = POLLIN; pfds[0].revents = 0;
    if (statusworker.fd >= 0) { pfds[n].fd = statusworker.fd; pfds[n].events = POLLIN; pfds[n].revents = 0; ++n; }
    rc = poll(pfds, n, -1);
    if (rc < 0) { if (errno == EINTR) continue; break; }
    while (XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); handle_xevent(&e); }
    if (n > 1 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
      RawpanelStatusData next = {0};
      int got = rawpanel_status_worker_receive(&statusworker, &next, stderr);
      if (got > 0) { rawpanel_status_data_destroy(&statusdata); statusdata = next; draw_all(); }
      else if (got < 0) {
        rawpanel_status_worker_stop(&statusworker, stderr);
        control_publish_capabilities(false);
        draw_all();
      }
    }
  }
  cleanup();
  return 0;
}

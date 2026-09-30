# rawpanel

`rawpanel` is the persistent desktop panel for rawmkit.

It is intentionally independent from the window manager. `rawm` remains the
window-management authority and publishes admitted state through its control
membrane. `rawpanel` consumes that state, presents tags/layout/title/status,
owns the XEmbed system-tray selection, and reserves screen work area through
standard EWMH dock struts.

`rawpanel` never reconstructs rawm tag/layout truth by walking windows. Focused
client titles are consumed from rawm's `_RAWM_CLIENTS` inventory rather than
observed independently from client window properties.

## Authority

`rawpanel` owns:

- persistent panel windows and presentation;
- native machine-status acquisition;
- ALSA status/control for the configured mixer source;
- the per-screen XEmbed system-tray selection and embedded-icon lifecycle;
- panel-local pointer interaction policy.

It does not own window placement, tags, layouts, focus, global keyboard grabs,
or session process supervision. Authoritative rawm state refreshes may redraw or
resize an already mapped panel, but they do not reassert its root stacking order;
fullscreen/managed stacking remains window-manager authority.

The tray reserves panel width only for XEmbed clients whose `XEMBED_MAPPED` flag is set. An enabled but empty tray therefore has no visible blank cell.
Embedded windows are kept in the X save-set so an abrupt panel disconnect cannot destroy client-owned tray windows; races with clients that withdraw or destroy an icon are contained at the X11 peer boundary.

## Control membrane

For independent input routers, rawpanel owns `_RAWPANEL_CONTROL_Sn` while alive
and publishes `_RAWPANEL_SUPPORTED_COMMANDS`. Visibility is always advertised.
Native volume commands are advertised only while a live status worker has one
unambiguous enabled ALSA source; rawpanel withdraws them if that worker is lost.
Selection ownership is the liveness boundary; no socket, PID file, or stale root
property is used.

## Configuration

Built-in defaults are layered with `/etc/rawpanel/rawpanel.conf`, then an
optional user override from `$XDG_CONFIG_HOME/rawpanel/rawpanel.conf` when
`XDG_CONFIG_HOME` is absolute, otherwise from
`~/.config/rawpanel/rawpanel.conf`. Missing default files are allowed. Use
`-c FILE` to bypass system/user discovery and apply exactly that explicit
overlay to built-in defaults. `-C` validates configuration without opening X11.

The schema is fail-closed: unknown settings, wrong types, and malformed button
or status policy are configuration errors rather than ignored compatibility
state.

## Build

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Status-source diagnostics belong to this authority and are available with `rawpanel --status-debug`.

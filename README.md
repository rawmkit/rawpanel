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
- the per-screen XEmbed system-tray selection;
- panel-local pointer interaction policy.

It does not own window placement, tags, layouts, focus, global keyboard grabs,
or session process supervision.

## Control membrane

For future input routers, rawpanel owns `_RAWPANEL_CONTROL_Sn` while alive and
accepts version-1 commands for visibility and native volume control. Selection
ownership is the liveness boundary; no socket, PID file, or stale root property
is used.

## Configuration

The default search path is `$XDG_CONFIG_HOME/rawpanel/rawpanel.conf`, then
`~/.config/rawpanel/rawpanel.conf`, with `/etc/rawpanel/rawpanel.conf` as the
system location when no home config path can be formed. Use `-c FILE` to select
an explicit file and `-C` to validate configuration without opening X11.

## Build

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Status-source diagnostics belong to this authority and are available with `rawpanel --status-debug`.

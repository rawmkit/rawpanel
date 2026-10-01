# NEWS

## 0.1.0
- Restore scalable monochrome Fontconfig fallback for Unicode glyphs absent from the configured primary Xft face; mixed-font runs are measured consistently and centered within the panel line.
- Stop passive rawm state reconciliation from raising an already mapped panel across fullscreen or other higher surfaces; state refresh now preserves existing root stacking.
- Preserve peer-owned XEmbed windows across abrupt panel loss with the X save-set, and contain disappearing-client X errors instead of terminating the panel.
- Clip text rendering to each allocated panel cell so long window titles cannot draw across status or tray surfaces.
- Center status separators between adjacent cells instead of applying cell padding to the separator glyph.
- Complete the XEmbed tray lifecycle: publish the tray visual, embed unmapped clients, track `_XEMBED_INFO` mapped state, and preserve icons across panel topology recreation.
- Consume rawm state version 2, including the producer-owned work-area rectangle, while retaining full-monitor geometry for panel placement.

- Establish standalone persistent-panel authority.
- Consume rawm authoritative monitor/tag/layout/stack state.
- Reserve work area through EWMH dock struts.
- Move native status acquisition and ALSA control into the panel authority.
- Own XEmbed system-tray selection independently from rawm.
- Expose a small selection-based panel control membrane.

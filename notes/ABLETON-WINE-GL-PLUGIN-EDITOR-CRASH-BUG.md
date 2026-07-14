# Crash opening a GL plugin editor

## Symptoms

Opening a JUCE/OpenGL VST3 editor (Chow Tape Model) breaks Live instantly:
the editor collapses to 1×1, rendering wedges, force-quit required. The log
stops after `VST3: Created: CHOWTapeModel`; stderr shows a fatal X protocol
error:

```
X Error of failed request:  BadMatch (invalid parameter attributes)
  Major opcode of failed request:  139 (RENDER)
  Minor opcode of failed request:  4 (RenderCreatePicture)
```

Deterministic; triggered by editor paint, not plugin load.

## Research

Wine composites an OpenGL child surface onto its top-level window in
`X11DRV_client_surface_present` (`dlls/winex11.drv/init.c`) via
`NtGdiStretchBlt` to a display DC. A fresh display DC starts on the depth-24
root pict format (`WXR_FORMAT_ROOT`). The present points the DC at the window
with `set_dc_drawable`, whose `X11DRV_SET_DRAWABLE` escape carries no visual
(`escape.visual == {0}`), which `xrenderdrv_ExtEscape` reads as "keep the
current format", so the DC stays depth-24.

Harmless for depth-24 windows. But this series gives high-DPI layered
plugin-editor top-levels a depth-32 ARGB visual (titlebar / drop-shadow work;
see
[ABLETON-WINE-PLUGIN-TITLEBAR-BUG.md](ABLETON-WINE-PLUGIN-TITLEBAR-BUG.md)).
A depth-32 window with a depth-24 format makes `XRenderCreatePicture` fail
with BadMatch (a window Picture's format must match the window's visual); the
default Xlib handler wedges the UI. Pinned by logging `ddepth=32 fmtdepth=24`
at the failing call.

## Mitigations

[../patches/0026-winex11-report-the-drawable-s-visual-in-set_dc_drawa.patch](../patches/0026-winex11-report-the-drawable-s-visual-in-set_dc_drawa.patch):

1. `init.c set_dc_drawable`: query the drawable's visual
   (`XGetWindowAttributes`) and fill `escape.visual`; the escape selects a
   pict format matching the actual depth, so a depth-32 window gets
   `A8R8G8B8`. Non-window drawables (GLX pbuffers) keep the old "keep format"
   behaviour.
2. `window.c X11DRV_ReleaseDC` (hardening): the escape's visual was
   uninitialized stack garbage, the same trap from a second angle; now set to
   the root window's default visual.

Verified: the editor opens at its proper size and renders; no X errors; Live
stays healthy.

## Caveats

Any GL-rendered plugin editor is affected, not just Chow: the trigger is a GL
client surface presented onto a depth-32 top-level, i.e. any layered high-DPI
plugin editor in this series. The depth-32 visuals come from the
titlebar/drop-shadow work; that feature and this fix belong together.

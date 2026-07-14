# Pianoteq half-size ghost flicker + unclosable JUCE modal

## Symptoms

Pianoteq's VST3 editor in Live flickers with a half-size copy of its
interface inside the same window; JUCE modals painted in the plugin window
can't be closed. Standalone Pianoteq is unaffected.

## Research

A window resize feedback loop. The editor window animates between ~724x707
and ~1183x1211 (logical), several steps per second. The "ghost" is the fresh
render at the current (smaller) size blitted top-left inside stale pixels
from the larger size; both copies show live content because both are recent
frames at different loop phases. The unclosable modal is clicks landing on
stale pixels.

Under "Auto-Scale Plugin Window", Live creates the `Vst3PlugWindow` toplevel
DPI-unaware (ctx `0x6010`, dpi 96; the main window is per-monitor @192). Wine
DPI-virtualizes the unaware toplevel (scaled surface ×2), and the VST3 size
negotiation (JUCE resizeView ⇄ Live SetWindowPos) never reaches a fixpoint:
physical frame metrics leak into logical space (the JUCE child sits at
y-offset 58 logical where the real frame is 33, a 56px physical WM titlebar
applied as logical), so every round-trip misses by the frame error, ~3
iterations/sec, triggered by editor open, zoom change, or manual resize.
Standalone Pianoteq runs fully DPI-aware; no loop.

Method: rate-limited present/resize probes show a marching buffer size with a
constant thread DPI context (kills the "DPI context flip" theory); a
`trace+win` volley shows the toplevel resized first, so the loop is
host-driven; [../tools/dpispy.c](../tools/dpispy.c) exposes the unaware
`Vst3PlugWindow` in one shot.

## Mitigations

- Right-click the device header, uncheck "Auto-Scale Plugin Window", reopen
  the editor. Live then hosts it per-monitor-aware. This is the fix; no Wine
  change needed.
- [../patches/0023-wined3d-dxgi-query-present-resize-client-rects-in-th.patch](../patches/0023-wined3d-dxgi-query-present-resize-client-rects-in-th.patch):
  present dst rect and `ResizeBuffers(0,0)` auto-size are queried in the
  window's DPI context, not the calling thread's (presents address physical
  pixels). Correctness hardening for mixed-DPI callers; not this bug's
  cause.
- [../patches/0024-wined3d-keep-present-resize-DPI-diagnostics-at-trace.patch](../patches/0024-wined3d-keep-present-resize-DPI-diagnostics-at-trace.patch):
  the probes, kept at trace level (`WINEDEBUG=trace+d3d`).

## Caveats

The fix is host configuration: any editor hosted DPI-unaware (Auto-Scale on)
can re-enter the loop; disabling Auto-Scale Plugin Window is a standard
first-launch step. Patch 0023 does not remove the loop for unaware-hosted
editors.

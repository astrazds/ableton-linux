# Learn View flicker / mangled rendering (OPEN)

## Symptoms

Live's Learn View (the WebView2 lesson panel, `Chrome_WidgetWin_1`
"Learn View 12") shows a band of stale, horizontally clipped content laid out
for a much wider viewport, mixed with correct regions, flickering between
states on activity. Everything else renders fine.

## Research

From `+dxgi` traces, dcompspy/hwndspy, and X pixel sampling:

1. Creation-size mismatch: Chromium creates the composition swapchain at the
   pane's transient initial size (`CreateSwapChainForComposition
   (1273x1552)`), then calls `ResizeBuffers(299x804)`, the real pane,
   ~400 ms later.
2. Stale-size paints: between creation and resize, WM_PAINT /
   `dcomp_reblit_comp_buffer` blit the old 1273-wide snapshot into the
   already-299-wide window; that crop of wide-layout content is the mangled
   band.
3. Correct paints never reach the screen. After the resize the comp buffer
   is correct and full-frame BitBlts demonstrably execute, yet X-side pixels
   never change (1 ms sampling: zero deltas). The Intermediate D3D Window has
   `WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT` and never
   calls SetLayeredWindowAttributes. On Windows, NOREDIRECTIONBITMAP means
   "no GDI surface, DWM shows the DComp visual directly"; in Wine, GDI blits
   to this never-attributed layered window are invisible. What shows instead
   is the sibling below, `Chrome_RenderWidgetHostHWND`: Chromium's software
   fallback frame, drawn once at the old geometry (hence the stale,
   wrong-stride shear), never updated because Chromium believes GPU
   compositing is active.
4. Window chain: `AbletonWebViewHelperWindow` (hidden) → `Chrome_WidgetWin_0`
   → `Chrome_WidgetWin_1` → siblings `Chrome_RenderWidgetHostHWND` (visible)
   + `Intermediate D3D Window` (visible, layered), all the same rect.

The doc-sidebar fix
([../patches/0022-dxgi-stop-forcing-swapchain-presents-from-the-dcomp-.patch](../patches/0022-dxgi-stop-forcing-swapchain-presents-from-the-dcomp-.patch))
doesn't cover this: it stops the reblit timer from forcing stale presents.
Here the presents are fine; the display path of the target window is the
problem, plus the stale-size window during Chromium's create-then-resize
dance.

## Mitigations

- [../patches/0030-dxgi-don-t-blit-stale-sized-dcomp-comp-buffers-into-.patch](../patches/0030-dxgi-don-t-blit-stale-sized-dcomp-comp-buffers-into-.patch)
  (partial, shipped): both blit sites skip when the comp-buffer size no
  longer matches the swapchain's current desc, which kills #2.
- Workaround: nudge the Learn View splitter; the resize forces a fresh
  ResizeBuffers + present cycle and usually heals the pane until the next
  reopen.
- Designed, not implemented (fixes #3): in `dcomp_swapchain_wndproc`, when
  subclassing a target with `WS_EX_NOREDIRECTIONBITMAP` (or LAYERED without
  attributes set), either strip `WS_EX_LAYERED` so blits land in the normal
  flat window surface, or set opaque layered attributes
  (`SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)`). Also consider
  hiding `Chrome_RenderWidgetHostHWND`'s stale software frame (DWM never
  shows it once GPU compositing is up).

## Caveats

- One blit can still race patch 0030 (`windowposchanged` reblit on one
  thread vs `ResizeBuffers` on another in the same ms); a one-shot delayed
  reblit would close it.
- Regression watch for the designed fix: JUCE DropShadower layered windows
  ([../patches/0015-win32u-sync-layered-attributes-to-the-scaled-surface.patch](../patches/0015-win32u-sync-layered-attributes-to-the-scaled-surface.patch)),
  SWAM plugin GUIs, the doc sidebar. Test at 100% and 125% display scale.
- Tools: [../tools/dcompspy.c](../tools/dcompspy.c),
  [../tools/hwndspy.c](../tools/hwndspy.c),
  [../tools/xdmg.c](../tools/xdmg.c), [../tools/xsamp.c](../tools/xsamp.c),
  [../tools/xgrid.c](../tools/xgrid.c),
  [../tools/xsettle.c](../tools/xsettle.c).

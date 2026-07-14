# Oversized plugin-editor title bar / native decorations

## Symptoms

At a 2× XWayland framebuffer (125% scale, `LogPixels=192`), VST plugin editor
windows get a giant Wine-drawn Win32 caption while the main window has a
proper native WM title bar. Related: JUCE popup menus and their DropShadower
windows render their soft shadows as opaque black rectangles.

## Research

Window anatomy, same process, same moment:

| | Main window | Plugin editor |
|---|---|---|
| class | `Ableton Live Window Class` | `Vst3PlugWindow` |
| style | `0x2cf0000` (overlapped, resizable) | `0xc80000` = WS_CAPTION\|WS_SYSMENU |
| ex_style | `0` | `0x80` = WS_EX_TOOLWINDOW |
| DPI space | 192 (per-monitor-aware) | 96 (unaware) |
| decoration | WM frame, single native bar | Wine-drawn caption, oversized |

The main window NCCALCSIZEs the standard caption away and fills the band with
its own menu; the WM frame is its only bar. The plugin window keeps the
default non-client area, and stock Wine gives `WS_EX_TOOLWINDOW` windows no
WM decorations (`get_mwm_decorations_for_style` returns 0), so Wine draws the
caption itself: DPI-unaware, surface-scaled 2×, huge.

The core split: the editor is created under an explicit
`SetThreadDpiAwarenessContext(UNAWARE)` (ctx `0x6010`, the standard VST3-host
trick) while the rest of the process is per-monitor-aware. Live's NCCALCSIZE
insets the window with 192-DPI metrics (top 58 in a 96-space window); Wine's
visible-rect allowance uses 96-DPI metrics (29). The 29-unit difference is
the caption band leaking into the X window, bitmap-scaled 2×.
[../tools/fakeplugin.c](../tools/fakeplugin.c) replicates the exact anatomy;
rects byte-identical to Live's.

Failed attempts, preserved as patches
[0010](../patches/0010-winex11-let-captioned-tool-windows-use-WM-decoration.patch)-[0013](../patches/0013-Revert-winex11-let-captioned-tool-windows-use-WM-dec.patch):

1. Request WM decorations for captioned tool windows (0010, reverted by
   0013): double title bar. With frame reconstruction off, the
   window-to-visible band (WM frame) and the visible-to-client band (Wine
   caption) both exist, so both paint.
2. Reconstruct `_NET_FRAME_EXTENTS` for tool windows so the WM frame absorbs
   the caption (0011, reverted by 0012): the window collapses to 0px. The
   extents arrive in physical (192) space, the window rect lives in unaware
   (96) space; subtracting ~56-93px physical from a 650px 96-space window is
   degenerate.

Take-away: never enable frame reconstruction for a window whose rects are in
a different DPI space than the extents.

## Mitigations

- [../patches/0014-win32u-winex11-give-captioned-tool-windows-the-nativ.patch](../patches/0014-win32u-winex11-give-captioned-tool-windows-the-nativ.patch):
  captioned tool windows request WM decorations, and `get_visible_rect` maps
  the X window to exactly the client rect: the whole Win32 caption band lands
  outside the X window and is never drawn; the WM bar is the only titlebar.
  No frame reconstruction; window/client rects untouched, so Live's own
  geometry math is unaffected. Verified: `visible == client`, a single
  native bar, draggable, SetWindowPos only on user drags, main window
  untouched.
- [../patches/0015-win32u-sync-layered-attributes-to-the-scaled-surface.patch](../patches/0015-win32u-sync-layered-attributes-to-the-scaled-surface.patch)
  (black shadow boxes): `win32u/dce.c scaled_surface_flush` forwards
  `color_key/alpha_bits/alpha_mask` to the target x11 surface only on shape
  changes; shadow windows have no shape, the surface never learns per-pixel
  alpha, and `x11drv_surface_flush` ORs `0xff000000` into every pixel
  (premultiplied mostly-transparent black turns solid black). Fix: sync
  layered attributes on every flush (no-op when unchanged).

## Caveats

- XGetImage-based screenshots flatten ARGB without blending; verify
  translucency on the real screen.
- The `visible == client` mapping gives editor top-levels depth-32 ARGB
  visuals, the precondition for the GL BadMatch crash fixed by
  [../patches/0026-winex11-report-the-drawable-s-visual-in-set_dc_drawa.patch](../patches/0026-winex11-report-the-drawable-s-visual-in-set_dc_drawa.patch)
  (see
  [ABLETON-WINE-GL-PLUGIN-EDITOR-CRASH-BUG.md](ABLETON-WINE-GL-PLUGIN-EDITOR-CRASH-BUG.md)).

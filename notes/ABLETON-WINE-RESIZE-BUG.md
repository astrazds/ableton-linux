# Main-window autosize/resize feedback loop (Wine bug 57955 tail)

## Symptoms

Live's main window runs a self-resize feedback loop at 192 DPI (125% display
scale, 2x XWayland framebuffer): Live computes its desired outer rect as
client + frame via `AdjustWindowRectExForDpi`, sets it with
`NtUserSetWindowPos`, reads the result back, finds the client area still
isn't what it wants, and re-requests, forever. Two regimes depending on patch
state:

- Growth: the window creeps ~2px per cycle past the monitor edge, unbounded.
- Spinning in place: the size converges but Live re-requests a no-op
  175-400x/sec, burning 80-99% of a core on MainThread; every cycle repaints
  the frame, producing a strobing white border.

## Research

Root cause, verified with instrumented win32u traces:

- Live's ALF uses the standard Windows mixed-mode DPI design: it deliberately
  leaves the process default unaware (it logs `Desired process DPI awareness:
  0`) and sets per-thread pm-v2 via `SetThreadDpiAwarenessContext`.
- Live's embedded CEF/Chromium (`Chrome_MessageWindow`) then grabs Wine's
  one-shot process-awareness latch with UNAWARE (`0x6010`) ~0.5 s into boot.
- The main window is created while the main thread is (transiently) pm-v2, so
  the window is per-monitor @192 forever. Wine re-switches a thread's DPI
  context only on hardware message dispatch (`win32u/message.c
  process_hardware_message`), unlike Windows, which switches on all message
  retrieval, so Live's main thread keeps falling back to the CEF-poisoned
  unaware default (96-space) while doing layout.
- Live therefore combines a 96-space client with 192-DPI frames,
  `map_dpi_winpos` doubles the request, the readback halves it, and expected
  client differs from actual client by construction: infinite re-request.

One spin cycle:

```
NtUserSetWindowPos hwnd 0x100a6, -3,8 (2054x1275), flags 0x14              <- Live, thread ctx UNAWARE (96-space)
map_dpi_winpos: thread_dpi 96 -> window_dpi 192: -> (-6,16)-(4102,2566)    <- doubled (window is pm-v2)
adjust_window_rect style 0x16cf0000 menu 1 dpi 192 -> frame (-5,-93)-(5,5) <- Live's own frame query, 192-scale
calc_winpos: old == new (-6,16)-(4102,2566), client (-1,109)-(4097,2561)   <- true no-op, ~150/sec
```

The trace also proves the loop is Live's own layout code re-driving
`NtUserSetWindowPos` (`old_rects == new_rects` on every call,
`WM_WINDOWPOSCHANGED` never sent), not a Wine/WM feedback, so it cannot be
broken by suppressing or altering messages. The 37px top inset red herring is
Live's real Win32 menu bar, passed `menu=1` by Live too: symmetric, never the
diverging term.

Dead ends, in order tried (the frame-extents arc is preserved as patches
[0006](../patches/0006-winex11-disable-frame-extents-reconstruction-comdlg3.patch)-[0009](../patches/0009-revert-frame-extents-re-enable-a5ab9f00-reintroduced.patch)):

1. Removing the HIGHDPIAWARE compat layer: no change.
2. `LogPixels 96`: the loop stops but the UI is unusably tiny under the 2x
   framebuffer.
3. The Wayland driver: popup/menu positioning breaks; not viable.
4. Clamping requested sizes to the monitor
   ([0007](../patches/0007-win32u-clamp-top-level-window-size-to-monitor-bug-57.patch),
   removed by
   [0008](../patches/0008-re-enable-frame-extents-round-trip-revert-patch-06-d.patch)):
   stops the growth; Live then spins 175/sec on the clamped no-op.
5. Re-enabling the `_NET_FRAME_EXTENTS` round-trip (0008): the size converges
   but the spin continues at 400/sec with a strobing frame; turned off again
   by 0009.
6. Dark border colors: hides the flicker; the CPU spin remains.

## Mitigations

One IFEO registry value in the prefix (applied by `setup-prefix.sh` as part
of the DPI matched set, only at scales with an upscaled framebuffer; see
[ABLETON-WINE-DPI-SCALE-100.md](ABLETON-WINE-DPI-SCALE-100.md)):

```
HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Ableton Live 12 Suite.exe
    dpiAwareness = REG_DWORD 2      (per-monitor aware; stock Wine user32 mechanism)
```

user32's `SYSPARAMS_Init` applies it at process attach, before any app code
runs. The process default becomes per-monitor: no 96-DPI space is left in the
process, CEF's unaware grab is rejected (`ERROR_ACCESS_DENIED`, as on a
Windows box where something set awareness first; Chromium handles it), Live
logs `Effective process DPI awareness: 2`, thread dpi = window dpi = 192, and
Wine's NCCALCSIZE inset equals Live's `AdjustWindowRectExForDpi` expectation,
so the cycle terminates after one pass.

Verified: `NtUserSetWindowPos` on the main window drops to 0/sec after ~15 s
of boot layout (previously 150-400/sec forever), with zero calls in the final
60 s of a 100 s trace; no thread above 0.5% CPU (previously 80-99%
MainThread); no strobing border, no growth; frame and dragging behavior
untouched.

## Caveats

- The IFEO key belongs only to upscaled-framebuffer scales; at 100% it is
  actively harmful (see the matched set in ABLETON-WINE-DPI-SCALE-100.md).
- Never change the prefix's `MachineGuid`; Live's offline authorization is
  bound to it.
- Keep Live's GPU renderer off (`Options.txt`: `-_ForceGdiBackend`); GPU-on
  reintroduces an intermittent blank file dialog (the explorerframe shell
  view starves under the renderer's compositing).
- Don't set the `Window` system color dark; it blacks out comdlg32 dialog
  lists. Only border/frame/edge colors are safe to darken.
- One Wine build per prefix at a time: wineserver protocol versions differ
  between builds; never let two builds drive the same prefix concurrently.
- No Wine virtual desktop.

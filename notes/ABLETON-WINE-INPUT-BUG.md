# Plugin editor input dead / menus won't open / shortcuts inert

## Symptoms

Three at once: SWAM VST3 editors render but ignore all mouse input;
application menus open-then-instantly-close, or not at all, with multi-second
freezes; keyboard shortcuts do nothing. Intermittently, `VST3: plug window
creation failed` in the log. A fourth, separate crash: nih-plug/baseview
plugin editors (Reel Deal) abort the whole process on open.

## Research

Three independent root causes, plus one for the baseview crash:

1. dcomp target-subclass black hole. The DirectComposition emulation
   subclasses the composition target HWND. JUCE 8's Direct2D renderer (SWAM)
   recreates its composition device+target on the same HWND; the second
   `CreateTargetForHwnd` records dcomp's own wndproc as "original" and the
   shared `__wine_dcomp_target` property is clobbered on release. End state:
   subclass installed, no target property, every message goes to
   `DefWindowProcW`. Painting survives (JUCE renders timer-driven D2D, no
   WM_PAINT needed) but all input is swallowed, including WM_NCHITTEST; the
   click-through JUCE overlay stops reporting HTTRANSPARENT and eats clicks.
2. `_NET_ACTIVE_WINDOW` requests with timestamp 0, no retry. winex11 sends
   activation requests with `data.l[1] = 0`; GNOME 50 mutter silently drops
   them (focus-stealing prevention). The pending-request dedup then
   suppresses every further request and the unacknowledged serial blocks
   foreground sync: one dropped request wedges activation for the whole
   session. Menus need activation, keyboard follows the compositor's focus,
   and clicks on a not-yet-active window die in the WM_MOUSEACTIVATE dance.
3. Stale shared-session views, NULL-wndproc window classes. Clients map
   wineserver's shared session memfd as PAGE_READONLY views; ntdll maps
   read-only views MAP_PRIVATE on Linux, which is not coherent for this
   memfd. Views go permanently stale: `find_shared_session_object` reads id 0
   where the server wrote a class object (verified against the memfd via
   `/proc/<wineserver>/fd`), `NtUserRegisterClassExWOW` fails, and window
   creation dies with a swallowed null-call AV in WM_NCCREATE; each AV burns
   ~2.4 s in Live's vectored crash handler (the "menu opens then freezes"
   feel), and the same mechanism kills `Vst3PlugWindow` creation.
4. baseview GL crash. The Wine 11.11 EGL backend hardcodes
   `framebuffer_srgb_capable = FALSE` (upstream TODO); baseview defaults to
   `srgb: true`, gets 0 formats from `wglChoosePixelFormatARB`, and panics in
   a cannot-unwind context, aborting the process.

## Mitigations

1. [../patches/0016-dcomp-never-let-an-orphaned-target-subclass-swallow-.patch](../patches/0016-dcomp-never-let-an-orphaned-target-subclass-swallow-.patch):
   keep the true original wndproc in its own property, never chain the
   subclass to itself, forward (never DefWindowProc) when the target struct
   is gone, tear down only state owned by the released target.
2. [../patches/0017-winex11-send-real-timestamps-in-_NET_ACTIVE_WINDOW-r.patch](../patches/0017-winex11-send-real-timestamps-in-_NET_ACTIVE_WINDOW-r.patch):
   send the last real input timestamp and re-send when a newer one exists;
   self-healing on user clicks.
3. [../patches/0019-win32u-map-shared-session-views-MAP_SHARED-read-writ.patch](../patches/0019-win32u-map-shared-session-views-MAP_SHARED-read-writ.patch)
   (decisive): map session views `SECTION_MAP_READ|SECTION_MAP_WRITE` +
   `PAGE_READWRITE` so ntdll uses MAP_SHARED (read-only fallback kept).
   [../patches/0018-server-pre-dirty-shared-session-mapping-pages-win32u.patch](../patches/0018-server-pre-dirty-shared-session-mapping-pages-win32u.patch)
   adds server-side pre-dirtying of grown session blocks and a `<` to `<=`
   block-match fix. Verified: 30 000 register-class + create/destroy-window
   iterations across mapping growths, zero failures; 0 session-object
   mismatches per boot (previously 10-12).
4. [../patches/0020-opengl-advertise-and-honor-sRGB-capable-pixel-format.patch](../patches/0020-opengl-advertise-and-honor-sRGB-capable-pixel-format.patch):
   advertise sRGB for 8-bit RGB formats when the display has
   `EGL_KHR_gl_colorspace`; create X11 EGL surfaces with
   `EGL_GL_COLORSPACE_SRGB_KHR` (plain retry fallback). Repro:
   [../tools/glchild.c](../tools/glchild.c) runs baseview's exact attrib list
   with/without the sRGB bit.

## Caveats

- All four are upstream-worthy. The MAP_PRIVATE read-only view coherence
  assumption in `ntdll/unix/virtual.c` is the deepest and may bite other
  shared-memory consumers.
- Diagnostic tools in [../tools/](../tools/): `swamprobe.c` (window
  tree/DPI/hit-test/focus), `liveinject.c` (Wine-internal SendInput; mouse
  reliable, synthetic keyboard is not, so don't use it for shortcut
  testing), `xrec.c`/`xmon.c`/`xact.c` (X focus tracing/prodding),
  `mousespy.c` + `spyhost.c` (WH_MOUSE hook with wndproc-to-module
  resolution, which found dcomp as the subclasser; never add WH_CALLWNDPROC
  hooks from the hook DLL, it wedges Live's UI thread),
  `menutest.c`/`stresstest.c` (standalone repros).
- Cheap canaries: `WINEDEBUG=+message` shows the black hole
  ("DefWindowProc:" nesting); `warn+winstation,err+class` flags the
  session-object failure; `+seh` shows the swallowed AVs.

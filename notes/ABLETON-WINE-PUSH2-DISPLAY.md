# Push 2 display: host libusb bridge

## Symptoms

Push 2 MIDI works end to end (Linux enumerates the device, ALSA exposes both
MIDI ports, Live identifies the controller, Wine registers
`VID_2982&PID_1967&MI_00`) but the display stays dark:
`Push2DisplayProcess.exe` makes twenty attempts to find the display and
exits.

## Research

Push 2 is a composite USB device; MIDI and display separate at the interface
boundary:

```
Push 2, 2982:1967
├── interface 0, vendor-specific display, bulk OUT 0x01 / IN 0x81
└── interfaces 1 and 2, USB audio control and MIDI streaming
```

Ableton's bundled libusb 1.0.23 Windows backend discovers interfaces through
SetupAPI and resolves WinUSB entry points. Wine 11.11 enumerates the USB
device but its WinUSB function layer is incomplete for this path, so the
failure sits after MIDI identity and Wine enumeration, before the helper can
open interface 0. A native probe
([../tools/push2usb.c](../tools/push2usb.c)) establishes the host-side
boundary: enumeration, claim/release cycles, and async IN cancellation all
pass while interfaces 1 and 2 stay on `snd-usb-audio`.

Findings that look like bridge faults but aren't:

- `Shutting down because live didn't ack in time` is the normal standalone
  helper lifecycle: without a responsive Live it opens the display, retries
  `sync` for ~8 s, and exits 0. The `*.push2-N` files under
  `Live Reports/Usage` are per-launch helper logs; `push2-0` with no
  successors is a clean run.
- Two `Push2` control-surface rows launch two helpers; the first claims
  interface 0, the second correctly gets `LIBUSB_ERROR_BUSY`. Changing rows
  while Live runs rebuilds the scripts but the original helper keeps its
  claim, so later helpers enumerate-but-can't-claim.
- If the audio stack is sick at Live startup (PipeWire refusing clients),
  mmdevapi driver init fails, winmm's MIDI part is disabled, and Live
  enumerates zero MIDI devices. The list is latched per process, so audio can
  recover mid-session while the MIDI list stays empty: no ports, no identity
  sysex, no display helper. A replug can't help a session that enumerated
  while the ports were absent
  ([patch 0028](../patches/0028-winealsa-re-subscribe-MIDI-devices-when-they-reappea.patch)
  re-attaches only ports present at startup).
- `amidi -l` reads rawmidi and proves nothing about Wine, which reads the
  sequencer graph; the discriminating host checks are `aconnect -l` and
  `pactl info`.
- Exonerated: wineusb.sys opens every host USB device (hotplug MATCH_ANY), so
  persistent `/dev/bus/usb` fds from idle prefixes are expected, but it never
  claims an interface, never detaches a kernel driver, and its
  `SELECT_CONFIGURATION` is a no-op; an open usbfs fd is inert.
  `push2_display.inf`'s WinUSB service is decorative: Wine ships no
  `winusb.sys`, so the device starts raw.

## Mitigations

[../patches/0032-libusb-1.0-add-host-USB-bridge-for-Push-2.patch](../patches/0032-libusb-1.0-add-host-USB-bridge-for-Push-2.patch):
a Wine builtin `libusb-1.0.dll` whose PE half preserves the Windows libusb
ABI used by the helper and whose Unix half calls host `libusb-1.0.so.0`:

```
Push2DisplayProcess.exe
  │  16-function Win64 libusb 1.0.23 ABI
  ▼
Wine builtin libusb-1.0.dll
  │  fixed-width WINE_UNIX_CALL requests
  ▼
Wine Unix library libusb-1.0.so
  │  host libusb
  ▼
/dev/bus/usb/... → interface 0 → endpoints 0x01 and 0x81

ALSA snd-usb-audio retains interfaces 1 and 2.
```

Wine selects the builtin only for the helper, via a per-application override
(applied by `setup-prefix.sh`); no Ableton file is replaced, and loader
tracing proves Live itself keeps its application-local DLL:

```
HKCU\Software\Wine\AppDefaults\Push2DisplayProcess.exe\DllOverrides
    libusb-1.0 = builtin
```

Implementation:

- Source under `dlls/libusb-1.0/`; `configure.ac` registers the module
  x86-64-only (a 32-bit PE half would need explicit WoW64 thunks; raw 64-bit
  host handles cannot cross into a 32-bit consumer).
- Exports the helper's exact 16 names with Ableton's original ordinals.
  Compile-time assertions enforce the 18-byte device descriptor, the 64-byte
  Win64 transfer object, and the observed field offsets; the PE side converts
  the two 32-bit `timeval` fields to the host's 64-bit fields.
- The host callback copies status and actual length back before dispatching
  the Windows callback; the helper can resubmit from that callback;
  cancellation drains through `libusb_handle_events_timeout` and produces
  exactly one cancelled callback.
- Unix exports use internal `wrap_*` names so symbolic binding cannot resolve
  a wrapper back to Wine's own library instead of the host's.

Configuration: exactly one control-surface row (Control Surface `Push2`,
Input and Output `Ableton Push 2 Live Port`). The User Port may remain a
plain MIDI device but must not be a second `Push2` row. After a stale
interface claim: save one row, close Live normally, relaunch.

Verification: [../tools/push2usb-pe.c](../tools/push2usb-pe.c) resolves the
16 exports, validates names against ordinals, and never sends an OUT
transfer:

```
push2-abi=ok exports=16 name-ordinal-pairs=16
push2-enumeration=ok 2982:1967
push2-claim=ok repetitions=2
push2-cancel=ok callbacks=1 status=cancelled actual_length=0
```

Integration: the standalone helper opens the display, streams, and exits
cleanly; Live loads its vendor DLL native while the helper loads the builtin;
the display opens on the first attempt with exactly one helper and no
`LIBUSB_ERROR_BUSY`; WineASIO opens in the same session; the container-built
artifact passes the same gates from a scratch prefix. ALSA MIDI ports and
driver bindings stay unchanged throughout.

Assembly lessons, now build gates in the pipeline:

1. A clean Wine `make install` ships no external add-ons; the first candidate
   omitted WineASIO and Live logged `Open: failed (loadAsioDriver)`. The
   pipeline builds and overlays WineASIO after Wine and compares file
   manifests (whole-tree hashes don't compare; rebuilt files legitimately
   differ).
2. Wine's configure silently drops `winealsa` without `libasound2-dev`; Wine
   then exposes zero hardware MIDI ports and the control surface never binds,
   while `aconnect -l` looks healthy the whole time. The container installs
   the dependency and `container-build.sh` fails the build if `winealsa.so`
   is absent.

Rollback: close Live and `Push2DisplayProcess.exe` first (a running process
does not re-evaluate DLL load order), then delete the override:

```
wine reg delete 'HKCU\Software\Wine\AppDefaults\Push2DisplayProcess.exe\DllOverrides' /v libusb-1.0 /f
```

## Caveats

- Deliberate scope limits; this is not a general libusb replacement:
  - PE32+ x86-64 only; interface 0 only; bulk transfers only, `flags == 0`,
    zero isochronous packets.
  - The bridge does not filter VID/PID or endpoints itself; safety depends on
    the per-application override plus the helper's own `2982:1967` /
    interface 0 selection.
  - Only `LIBUSB_OPTION_LOG_LEVEL` is accepted by `libusb_set_option`.
  - `libusb_free_device_list(list, 1)` is the supported contract; retaining
    proxy devices with `(list, 0)` is not.
  - Callback-dispatch failure is logged but cannot be returned to the PE
    event loop.
  - Outstanding transfers must be cancelled and drained before free or exit.
- Still open: physical-panel acceptance, `Push3.exe` loader isolation (the
  three consumers do not import the same libusb surface), a rollback
  exercise, and disconnect/`NO_DEVICE`/hotplug recovery inside the bridge.
- A Live session that enumerated MIDI while the ports were absent stays deaf
  for its lifetime; restart Live after the audio stack recovers.
- `loadAsioDriver failed` + empty MIDI has three distinct causes: a corrupt
  Ableton `Preferences.cfg` audio block (hard-killing Live mid-write can
  cause this; isolate with a config swap), a missing `winealsa.so`
  (`find <runtime> -iname 'winealsa*'`), or a transient PipeWire outage.
  Check all three before touching the prefix, and avoid `kill -9` on Live.
- Never run a prefix that forces `libusb-1.0=builtin` against a runtime
  without patch 0032; the override would select a builtin that does not
  exist.
- Raw helper/Live traces can contain the controller serial number; don't
  publish them. Identify hardware by VID/PID, interface, and endpoint.

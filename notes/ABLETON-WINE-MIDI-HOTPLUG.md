# MIDI controller disconnect/reconnect (hotplug)

## Symptoms

Unplug a MIDI controller while Live runs, plug it back in: the controller
stays dead (no input, no LED feedback) until Live restarts. On Windows the
same replug reconnects.

## Research

Two hotplug gaps in Wine's ALSA MIDI driver:

1. `alsa_midi_init()` enumerates sequencer ports once per process
   (`init_done` latch); the winmm device table is a snapshot.
2. Each open midi in/out is subscribed to a fixed seq `client:port` address.
   On unplug the kernel client disappears and ALSA silently drops the
   subscription; nothing re-subscribes, even when the device returns at the
   identical address (verified).

## Mitigations

[../patches/0028-winealsa-re-subscribe-MIDI-devices-when-they-reappea.patch](../patches/0028-winealsa-re-subscribe-MIDI-devices-when-they-reappea.patch)
(`dlls/winealsa.drv/alsamidi.c`):

- `seq_open()` subscribes the driver's shared input port to the System
  Announce port (0:1), so the record thread sees port lifecycle events.
- On `SND_SEQ_EVENT_PORT_START`: query the new port, rebuild the winmm
  display name ("client - port"), re-match against the enumerated
  srcs/dests, adopt the new address, re-subscribe any open input/output.

Works at the same or a different client id, for raw kernel seq ports and
PipeWire Midi-Bridge ports (name-keyed). Verified without hardware:
[../tools/fakectl.c](../tools/fakectl.c) (fake controller; kill + restart =
replug) + [../tools/midihot.c](../tools/midihot.c) (winmm midi-in listener
PE). Old driver: MIM_DATA freezes permanently after replug. Patched: resumes
<1 s, including at a changed client id.

## Caveats

- Only devices present at enumeration are re-attached; a never-seen device
  still needs a Live restart (dynamic table growth would race the unlocked
  dests/srcs indexing on other threads).
- Announce processing rides the record thread, which runs only while ≥1 MIDI
  input is open. Live keeps enabled inputs open; fine in practice.
- Identically-named controllers: the first table entry wins.

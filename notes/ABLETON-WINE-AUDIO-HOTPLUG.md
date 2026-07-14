# Audio device disconnect/reconnect

## Symptoms

Unplug and replug an audio interface while Live runs: the ports return, Live
stays silent until its audio engine is restarted.

## Research

Not a Wine bug. Live's ASIO "device" is the PipeWire-JACK graph, which
survives hardware changes; Wine never sees the unplug. What dies are the
JACK links between wineasio's ports and the hardware ports: PipeWire
destroys them with the device node, and on replug neither PipeWire nor
WirePlumber restores JACK links (restore logic covers pulse/native streams
only). Null-sink repro: after unload+reload the ports return, `pw-link -l`
shows no links.

## Mitigations

[../tools/jacklinkd.c](../tools/jacklinkd.c), started by the launcher: a
JACK client with no ports of its own that

- tracks every link in the graph (seeded at startup, port-connect callback);
- treats a port that unregisters within 5 s of its links being torn down as
  device death and remembers its links by port name;
- re-creates the links when a port with a remembered name registers again,
  retrying as ports appear one by one;
- never restores manual `pw-link -d` / patchbay disconnects.

Graph mutations run on the main thread only (JACK forbids them in
callbacks); it reconnects if PipeWire restarts. Live-agnostic: it guards
every JACK link, including routing outside Live.

Verified with a null-sink fake interface: links back within ~1 s of replug; a
manual disconnect afterwards stays disconnected.

## Caveats

- Restores only links it has seen; it can't invent routing for a
  never-connected device.
- Name-matched: a device that renumbers its ports on replug (rare under
  PipeWire) doesn't re-match; identically-named devices collide.
- Sample-rate mismatch on replug is resampled by PipeWire as usual. Live
  requesting a rate the graph doesn't run at is the separate wineasio clamp
  in [../patches/wineasio/](../patches/wineasio/) (previously ASE_NoClock,
  startup crash).

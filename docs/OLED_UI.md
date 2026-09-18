# HEALINK OLED UI

The SSD1306 is a presentation layer. The UI thread owns the framebuffer and
renders snapshots/events without acquiring sensors or modifying fusion,
classifier, safety, or communication state.

## Normal startup

The default `./healink` path uses a compact approximately six-second identity
sequence and then transitions to the live HUD.

`--demo` runs the longer cinematic presentation sequence. During normal
operation, `D` replays the presentation sequence without restarting the system.

## Live HUD

The live view presents only values whose validity state permits them to be
shown. Invalid numeric values render as `--`; OFFLINE and STALE remain
explicit states.

## LoRa event overlays

The UI can surface the critical communication events:

- SOS queued/transmitting and waiting for acknowledgement.
- LoRa ACK confirmed, including event ID and RTT.
- LoRa ACK failure, including event ID and elapsed time.

Critical LoRa notifications are downstream presentation events; the UI does
not become the emergency decision maker.

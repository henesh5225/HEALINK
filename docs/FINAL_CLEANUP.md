# Final source cleanup

This repository is the maintained HEALINK3 source tree used for the final build and hardware demo.

## Kept

- Production QNX application
- Current sensor drivers and platform helpers
- LoRa communication worker and ESP32 remote firmware
- One combined hardware diagnostic
- Fusion and safety test suite
- SX1278 probe/repeat tools
- A10 lifecycle soak script
- Current architecture, wiring, build and test notes

## Removed

- Old sensor drivers that are no longer part of HEALINK3
- Duplicate test projects and copied source trees
- One-off sensor debug programs
- Old SX1278 experiment variants
- Standalone ECG test programs
- Generated binaries, object files and build folders
- Patch and temporary note files from earlier bring-up work

The remaining files are intended to be understandable on their own. The source keeps comments focused on hardware details, timing decisions and non-obvious safety logic.

The final build clears the event queue at startup and only generates manual SOS events from the GPIO24 button or keyboard `S`.

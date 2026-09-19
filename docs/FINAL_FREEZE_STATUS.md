# Final freeze status

This source tree is the manual LoRa demonstration configuration.

- LoRa SOS is triggered only by the QNX physical SOS button (GPIO24) or QNX keyboard `S`.
- Sensor `FALL_DETECTED`, `EMERGENCY`, and `DISTRESS` events are still processed, but do not start LoRa transmission in this configuration.
- The event queue is recreated at startup so events left by a previous run are not replayed.
- The SOS button thread learns the real GPIO level at startup and does not treat the initial level as a button press.
- MQ135 has been removed from the project.
- The old Hello World source is not part of the project.
- Generated build products are not part of the source archive.

Before calling the software completely frozen, run one clean QNX/Momentics build and the final hardware demonstration on the target.

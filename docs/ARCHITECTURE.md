# HEALINK architecture

PS#24 requires a QNX Raspberry Pi platform that fuses heart rate, SpO2, temperature and motion, processes different-rate sensor streams, and identifies stale/inconsistent data.

```text
MAX30102  ---> PPG HR + SpO2 ----\
AD8232    ---> ECG HR ------------+--> timestamped shared state
MPU6050   ---> motion/fall -------+            |
DS18B20   ---> temperature -------/            |
                                             +-- Staleness
                                             +-- ECG/PPG consistency
                                             +-- confidence
                                             +-- health classifier
                                                     |
                                                     +--> OLED / CLI
                                                     +--> event queue
                                                            |
                                                            v
                                                     Emergency path
```

## IPC rule

Continuous state: shared memory protected by a process-shared mutex.

Discrete events: POSIX message queue (`/healink_events`).

## Timing rule

The fresh-start implementation uses monotonic absolute sleeps for periodic loops so the schedule does not accumulate drift. The production target can be moved to QNX POSIX timers/pulses once the hardware event path is validated.

## Safety boundary

The ML layer, if later added, must not be the sole emergency decision maker. Sensor validity, staleness, confidence and deterministic safety rules remain in the QNX control path.


## A7 deterministic safety path

A7 adds a bounded safety policy after fusion/classification. Falls escalate immediately; severe distress must remain present for a small fixed number of fusion cycles before an `EVENT_EMERGENCY` is emitted. Alert/distress/emergency actions are latched to prevent event flooding, and stable recovery clears those latches after a short confirmation window. The emergency decision remains deterministic and independent of any ML component.

## A9 fault / recovery validation

The deterministic test suite covers transient severe distress confirmation, one-shot alert/distress/emergency actions, recovery-latch clearing only after the configured confirmation window, recovery-window reset when an alarm returns, fall emergency latching and recovery, and injected sensor FAULT/STALE lifecycle transitions. These tests operate on the state/safety layers and do not introduce synthetic sensor readings into the real hardware runtime.

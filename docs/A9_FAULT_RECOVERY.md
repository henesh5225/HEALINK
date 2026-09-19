# A9 — Fault Injection and Recovery Validation

## Purpose

A9 verifies that HEALINK's deterministic safety and temporal-quality layers behave correctly when sensor state degrades and later recovers.

## Test boundary

These tests inject state into the fusion/staleness/safety functions only. They do not modify the production runtime to generate synthetic sensor readings.

## Coverage

- Severe distress requires `HEALINK_DISTRESS_CONFIRM_CYCLES` consecutive safety cycles before emergency escalation.
- Alert, distress, and emergency actions are latched and do not flood repeated events.
- Stable recovery requires `HEALINK_RECOVERY_CONFIRM_CYCLES` consecutive healthy cycles before clearing latches.
- A renewed alarm resets an in-progress recovery window.
- A confirmed fall causes immediate emergency action and can recover only after the configured healthy-state confirmation window.
- A runtime `FAULT` remains distinct from `STALE`.
- A stale sensor can return to fresh/valid state after a new timestamp is accepted by the acquisition layer.

## Required validation

Build from a QNX-enabled shell:

```sh
make clean
make -j12 all
make test
```

A9 is complete when the test target passes with zero compiler warnings/errors and the real-QNX runtime continues to preserve the OFFLINE/no-synthetic-data behavior and clean shutdown demonstrated by A8.

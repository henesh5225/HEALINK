# Robustness and Long-Duration Validation

## Purpose

A10 validates that the current HEALINK QNX runtime remains stable across repeated real process lifecycles. The validation is intentionally operational rather than feature-expanding: it checks deterministic tests, startup completion, sustained execution, clean shutdown, and restart integrity.

No production sensor values are synthesized or injected by the A10 harness.

## Scope

A10 validates:

- deterministic fusion/classifier/staleness/safety regression tests;
- repeated `healink` startup on the real QNX Raspberry Pi target;
- sustained runtime for a configured interval per cycle;
- clean SIGINT-driven shutdown;
- return to the `HEALINK OFFLINE` terminal state;
- absence of the previously observed stale shared-memory startup error;
- absence of deadline misses and schedule skips reported by the runtime;
- absence of obvious process/resource-integrity failures in the captured log.

## Run on QNX

From the HEALINK project directory:

```sh
chmod +x tools/qnx_a10_soak.sh
sh tools/qnx_a10_soak.sh 3 60
```

Arguments are:

```text
1: number of lifecycle cycles (default 3)
2: seconds of sustained runtime per cycle (default 60)
```

For a longer engineering soak:

```sh
sh tools/qnx_a10_soak.sh 10 300
```

The harness executes the real `./healink` binary. It does not add simulation or substitute hardware readings.

## PASS criteria

A10 is PASS only when every requested cycle:

1. starts successfully and prints `[HEALINK] CORE STARTED`;
2. survives the requested runtime interval;
3. exits successfully after SIGINT;
4. prints `[HEALINK] OFFLINE` during shutdown;
5. does not report `healink_state_create` / `File exists` startup failure;
6. reports no non-zero `deadline_miss` or `schedule_skip` timing counters;
7. produces no obvious fatal/resource-integrity diagnostic.

The existing deterministic test must also print:

```text
HEALINK fusion tests: PASS
```

## Interpretation with hardware absent

When the health sensors are not physically connected, the expected runtime state remains OFFLINE/unknown and measurements remain zero because no synthetic data is generated. This does not constitute a hardware-validation pass; it only validates lifecycle and software robustness of the no-hardware path.

Physical sensor behavior remains an A11 responsibility.

## Evidence to retain

For the final project evidence, retain the complete output from:

```sh
./healink_fusion_test
sh tools/qnx_a10_soak.sh 10 300
```

and, after hardware is available, the corresponding A11 hardware bring-up logs.

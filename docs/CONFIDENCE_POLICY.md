# HEALINK Confidence Coverage Policy

## Problem

A temperature-only runtime was producing a valid DS18B20 measurement while the generic arithmetic mean of active confidence channels reported `overall_confidence=1.00`. The classifier could then report `RESTING` even though no HR or SpO2 signal was present.

## Policy

- HR and SpO2 are the physiological core.
- Temperature and motion remain valid evidence, but they do not by themselves constitute a complete physiological assessment.
- With zero physiological channels observed, overall confidence is capped at `0.25`.
- With exactly one physiological channel observed, overall confidence is capped at `0.65`.
- With both physiological channels observed, no coverage cap is applied; signal quality and consistency still control confidence.
- The classifier reports `HEALTH_SENSOR_UNCERTAIN` when no physiological channel is observed or overall confidence is below `HEALINK_CONF_PHYSIOLOGY_THRESHOLD`.

## Expected DS18B20-only behavior

```text
DS18B20 ONLINE
Temp 27.8 C
VALID=0x00000008
OFFLINE=0x00000007
overall_confidence <= 0.25
health_state = SENSOR_UNCERTAIN
activity = UNKNOWN
```

This does not invalidate the temperature reading. It only prevents the UI and safety/classification layer from representing a single environmental modality as complete health-fusion confidence.

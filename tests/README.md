# HEALINK deterministic tests

`test_fusion.c` is the single maintained software regression suite for the
fusion, classifier, staleness, and deterministic safety layers.

It is intentionally separate from target hardware drivers: the test suite
checks decision logic without fabricating runtime sensor measurements.

Build from the project root:

```sh
make test
```

The current test fixture uses the production `MPU6050` motion model rather
than the superseded legacy motion model.

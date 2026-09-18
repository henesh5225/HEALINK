#!/bin/sh
# HEALINK A10 — QNX restart/soak validation
# Usage: sh tools/qnx_a10_soak.sh [cycles] [seconds_per_cycle]
# Must be executed on the QNX Raspberry Pi target from the HEALINK directory.
# This script performs real executions of ./healink only. It never injects
# or generates sensor measurements.

set -u

CYCLES=${1:-3}
DURATION=${2:-60}
LOG_DIR="/tmp/healink_a10_$$"

fail() {
    echo "[A10][FAIL] $*" >&2
    exit 1
}

case "$CYCLES" in
    ''|*[!0-9]*) fail "cycles must be a positive integer" ;;
esac
case "$DURATION" in
    ''|*[!0-9]*) fail "seconds_per_cycle must be a positive integer" ;;
esac
[ "$CYCLES" -ge 1 ] || fail "cycles must be >= 1"
[ "$DURATION" -ge 1 ] || fail "seconds_per_cycle must be >= 1"

mkdir -p "$LOG_DIR" || fail "cannot create $LOG_DIR"
trap 'rm -rf "$LOG_DIR"' EXIT HUP INT TERM

[ -x ./healink_fusion_test ] || fail "./healink_fusion_test not found or not executable"
[ -x ./healink ] || fail "./healink not found or not executable"

if ! ./healink_fusion_test >"$LOG_DIR/test.log" 2>&1; then
    cat "$LOG_DIR/test.log"
    fail "deterministic fusion/safety test failed"
fi

if ! grep -q "HEALINK fusion tests: PASS" "$LOG_DIR/test.log"; then
    cat "$LOG_DIR/test.log"
    fail "expected test PASS marker not found"
fi

echo "[A10] deterministic tests: PASS"
echo "[A10] cycles=$CYCLES duration=${DURATION}s"
echo "[A10] real hardware mode only; no synthetic sensor data"

cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
    log="$LOG_DIR/cycle_${cycle}.log"
    echo "[A10] cycle $cycle/$CYCLES: starting ./healink"

    ./healink >"$log" 2>&1 &
    pid=$!

    # Give the application a bounded startup window.
    startup_wait=0
    started=0
    while [ "$startup_wait" -lt 10 ]; do
        if grep -q "\[HEALINK\] CORE STARTED" "$log" 2>/dev/null; then
            started=1
            break
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        sleep 1
        startup_wait=$((startup_wait + 1))
    done

    if [ "$started" -ne 1 ]; then
        cat "$log"
        kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        fail "cycle $cycle did not reach CORE STARTED"
    fi

    sleep "$DURATION"

    if kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || fail "cycle $cycle failed to send SIGINT"
    else
        cat "$log"
        fail "cycle $cycle terminated before requested duration"
    fi

    wait_status=0
    wait "$pid" || wait_status=$?

    if [ "$wait_status" -ne 0 ]; then
        cat "$log"
        fail "cycle $cycle exited with status $wait_status"
    fi

    if ! grep -q "\[HEALINK\] OFFLINE" "$log"; then
        cat "$log"
        fail "cycle $cycle did not reach clean HEALINK OFFLINE shutdown"
    fi

    if grep -Eq "healink_state_create:|File exists|segmentation fault|SIGSEGV|core dumped" "$log"; then
        cat "$log"
        fail "cycle $cycle reported a startup/resource integrity failure"
    fi

    if grep -Eq 'deadline_miss=[1-9][0-9]*|schedule_skip=[1-9][0-9]*' "$log"; then
        cat "$log"
        fail "cycle $cycle reported a deadline miss or schedule skip"
    fi

    if grep -Eq '^[[:space:]]*[^[]*(ERROR|FATAL):' "$log"; then
        cat "$log"
        fail "cycle $cycle contains an ERROR/FATAL diagnostic"
    fi

    echo "[A10] cycle $cycle: PASS"
    cycle=$((cycle + 1))
done

echo "[A10] PASS — $CYCLES real-QNX lifecycle cycles completed"
echo "[A10] logs were stored temporarily in $LOG_DIR"
exit 0

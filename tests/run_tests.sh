#!/bin/sh
# SEED (MPS-16 ver FM1) in the host simulator: COACH, PICK NOTES, the pads, the B and C pads, LATCH, LIB, SYS.
# Exits non-zero on a failed expectation.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
cd base
for t in seed; do
    out=$(../app/build/host/mps-16_host "../app/tests/$t.txt" ../app/build/host 2>&1)
    echo "$out" | grep -E "FAIL|host:" || true
    if echo "$out" | grep -q "FAIL\|EXPECTATIONS FAILED"; then
        echo "tests: $t failed"
        exit 1
    fi
done
echo "tests: all passed"

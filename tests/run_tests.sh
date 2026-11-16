#!/bin/sh
# MPS-16 ver FM1's tests in the host simulator: Milestone 1 (pads, keys, lights, saving) and Milestone 2 (the drive,
# MIDI files, slicing, phrase pads, REC). Exits non-zero on a failed expectation.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
cd base
for t in basic m2 m3 m4 guide orchid probe bridge midi; do
    out=$(../app/build/host/mps-16_host "../app/tests/$t.txt" ../app/build/host 2>&1)
    echo "$out" | grep -E "FAIL|host:" || true
    if echo "$out" | grep -q "FAIL\|EXPECTATIONS FAILED"; then
        echo "tests: $t failed"
        exit 1
    fi
done
echo "tests: all passed"

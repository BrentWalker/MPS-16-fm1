#!/bin/sh
# ChordSmith FM: build with fm1-base. ./build.sh (firmware), --host (the simulator), --web (the browser), --release X.Y
exec "${PYTHON:-python3}" base/tools/build.py app "$@"

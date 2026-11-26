# MPS-16 ver FM1: SEED

Free firmware for the M-VAVE FM-1 by BeatSmith, in the Earth Science family. SEED is MPS-16's chord builder: play a
few notes you like, pick a scale that fits, and the 16 white keys (A pads) hold that key's chords and progressions.
The first 7 black keys (B pads) play the notes of the key, lit by the chord you played (the bass bright). The last 4
(C pads) are BASS, NO BASS, NO ROOT and LATCH. COACH, a step screen, walks you through it with the keys and buttons
it means lit. Everything goes out over USB MIDI.

From 0.30 the FM1 version is SEED only. The earlier MIDI phrase sampler (0.25 and before) is on the
`sampler-0.25` branch and stays on the site under /v/.

**Status:** 0.30, test builds. Install it or try it in the browser: https://brentwalker.github.io/MPS-16-fm1/ (older versions for testing: /v/VERSION/install/). Manual: [app/docs/MANUAL.md](app/docs/MANUAL.md).

## Built on

- [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0), with [felucca-bsp](https://github.com/hugelton/felucca-bsp) inside it: the platform layer for the FM-1.
- [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT): synth voices.

## Layout

| Path | What |
| --- | --- |
| `app/` | MPS-16 ver FM1 itself |
| `base/` | fm1-base, as a git submodule |

## Build

Clone with submodules:

    git clone --recursive https://github.com/BrentWalker/MPS-16-fm1

Host simulator (any Linux or macOS machine with a C compiler and Python 3):

    cd base && python3 tools/build.py ../app --host

Tests: `sh tests/run_tests.sh` (host simulator: app/tests/seed.txt). Firmware builds run on GitHub Actions through
the shared FM-1 builder: "Build and attest firmware", or "Publish site" to rebuild the installer and emulator (it
asks for a version). See fm1-base's README for the toolchain.

## Licence

fm1-base and felucca-bsp files are MPL-2.0. The licence for MPS-16 ver FM1's own code is still to be chosen.

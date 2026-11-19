# MPS-16 ver FM1

Free firmware for the M-VAVE FM-1 by BeatSmith, in the Earth Science family. A MIDI phrase sampler: load a MIDI file and it is on the 16 A pads (the white keys) at once, in its own key. The B pads (the first 7 black keys) play bass and lead in the key, the C pads (the last 4) change key for a bridge, MUTATE makes repeated chords more interesting, and each pad group sends MIDI on its own channel.

MPS-16 ver FM1 is the hardware version of MPS-16 ver WEB, and groundwork for the MPS-16 plugin.

**Status:** 0.25, test builds. Install it or try it in the browser: https://brentwalker.github.io/MPS-16-fm1/ (older versions for testing: /v/VERSION/install/). Manual: [app/docs/MANUAL.md](app/docs/MANUAL.md).

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

Tests: `sh tests/run_tests.sh` (host simulator: tests/basic, m2, m3). Firmware builds run on GitHub Actions through
the shared FM-1 builder: "Build and attest firmware", or "Publish site" to rebuild the installer and emulator (it
asks for a version). See fm1-base's README for the toolchain.

## Licence

fm1-base and felucca-bsp files are MPL-2.0. The licence for MPS-16 ver FM1's own code is still to be chosen.

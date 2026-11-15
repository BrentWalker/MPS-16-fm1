# MPS-16 ver FM1

Free firmware for the M-VAVE FM-1 by BeatSmith. It turns the FM-1 into a chord and phrase player: 16 pads on the white keys, scale degrees on the black keys, a chord library built from any key, MIDI FX (strum, arp, humanize, inversion) and MIDI file slicing.

MPS-16 ver FM1 is the hardware version of MPS-16 ver WEB, and groundwork for MPS-16.

**Status:** early test builds (0.3). Try it in the browser or install it: https://brentwalker.github.io/MPS-16-fm1/
(the site needs GitHub Pages on: Settings > Pages > gh-pages). Manual: app/docs/MANUAL.md.

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

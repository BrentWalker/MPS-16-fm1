# ChordSmith FM

Free firmware for the M-VAVE FM-1 by BeatSmith. It turns the FM-1 into a chord and phrase player: 16 pads on the white keys, scale degrees on the black keys, a chord library built from any key, MIDI FX (strum, arp, humanize, inversion) and MIDI file slicing.

ChordSmith FM is the hardware version of ChordSmith Web, and groundwork for MPS-16. Working title.

**Status:** early development. Not ready to install.

## Built on

- [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0), with [felucca-bsp](https://github.com/hugelton/felucca-bsp) inside it: the platform layer for the FM-1.
- [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT): synth voices.

## Layout

| Path | What |
| --- | --- |
| `app/` | ChordSmith FM itself |
| `base/` | fm1-base, as a git submodule |

## Build

Clone with submodules:

    git clone --recursive https://github.com/BrentWalker/chordsmith-fm

Host simulator (any Linux or macOS machine with a C compiler and Python 3):

    cd base && python3 tools/build.py ../app --host

Firmware builds run on GitHub Actions through the shared FM-1 builder. See fm1-base's README for the toolchain.

## Licence

fm1-base and felucca-bsp files are MPL-2.0. The licence for ChordSmith FM's own code is still to be chosen.

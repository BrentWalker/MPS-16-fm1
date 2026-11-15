# MPS-16 ver FM1

MPS-16 ver FM1 turns the M-VAVE FM-1 into a chord and phrase player. The 16 white keys are pads, the black keys are
the scale's degrees, and the keys light up with every chord's root. Copy MIDI files onto the FM-1 like a USB drive,
or record MIDI into it, then chop them into slices like audio in a sampler and drop the slices on pads. A chord
library builds chords and progressions in any key, and strum, arp, humanize and inversion shape every chord.

![MPS-16 ver FM1's HOME screen: pad 2 playing Bm7b5, ii in A minor, its notes, the 16 pads and the seven degree keys](manual/img/screen-playing.png "MPS-16 ver FM1 on the FM-1's screen")

This is version {{VERSION}}, an early test build by BeatSmith. You install it at your own risk, and the installer can
put M-VAVE's own firmware back.

- [Try it in the browser](emu/). It runs the same code as the FM-1, with sound.
- [Install](install/) it from Chrome or Edge on a computer, with the FM-1 connected by USB.
- The [Manual](manual/) explains the pads, slicing, the library, the key and the FX.
- [Download {{PKG}}](firmware/{{PKG}}) for the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/BrentWalker/MPS-16-fm1). It's built on fm1-base by Charles Vestal and
  felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. MPS-16 ver FM1 is not affiliated with M-VAVE.

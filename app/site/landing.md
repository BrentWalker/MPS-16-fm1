# SEED, MPS-16 ver FM1

SEED turns the M-VAVE FM-1 into the chord builder of MPS-16. Play a few notes you like, pick a scale that fits, and
the 16 white keys hold that key's chords and progressions. The black keys play the notes of the key and light up the
bass of every chord, and the last four are BASS, NO BASS, NO ROOT and TAP. One job a button, as MPS-16 ver WEB: pad edit, lock, latch, undo, record, save, and MIDI out over USB and Bluetooth with clock. COACH walks you through it on the
first start, lighting the keys and buttons it means.

![SEED's home screen: a progression playing, the B pads in colour, the 16 pads and the knobs](manual/img/seed-home.png "SEED on the FM-1's screen")

This is version {{VERSION}}, an early test build by BeatSmith. You install it at your own risk, and the installer can
put M-VAVE's own firmware back.

- [Try it in the browser](emu/). It runs the same code as the FM-1, with sound.
- [Install](install/) it from Chrome or Edge on a computer, with the FM-1 connected by USB.
- The [Manual](manual/) explains COACH, the pads, PICK NOTES and the rest.
- [Download {{PKG}}](firmware/{{PKG}}) for the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/BrentWalker/MPS-16-fm1). It's built on fm1-base by Charles Vestal and
  felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. MPS-16 ver FM1 is not affiliated with M-VAVE.

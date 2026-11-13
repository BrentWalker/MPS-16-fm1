"""Make /tmp/disk_computer.img: the drive as a computer would leave it (a long file name, a text file, macOS extras).
Needs: pip install pyfatfs. Run m2.txt first (it writes /tmp/chordsmith_disk.img), then this, then m2_disk.txt."""
import shutil
from pyfatfs.PyFatFS import PyFatFS
shutil.copy('/tmp/chordsmith_disk.img', '/tmp/disk_computer.img')
f = PyFatFS('/tmp/disk_computer.img', read_only=False)
f.writebytes('/My Neo Soul Keys.mid', open('midi/neosoul.mid', 'rb').read())
f.writebytes('/notes.txt', b'not midi')
f.makedir('/.fseventsd')
f.writebytes('/._My Neo Soul Keys.mid', b'\x00\x05\x16\x07appledouble')
f.close()

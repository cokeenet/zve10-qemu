#!/usr/bin/env python3
"""Check the /setting image for the two files the env.txt route depends on.

The env.txt delivery path is gated on /setting/mode/dmode existing: init's
check_mode_file opens it and only then sets the flag that makes main call
set_oreore_env. Without that file, env.txt is never read, which is why earlier
attempts saw no LD_PRELOAD in im.elf despite writing the file.

This walks the FAT32 directory tree of a nflasha2 image directly (no mount, no
vfat driver) and reports, for each candidate image:

  /setting/mode/dmode          exists?  <- gate for set_oreore_env
  /setting/env.txt             exists?  <- the value that gets exported
  /setting/mode/preload        exists?  <- bootin's debug preload path
  /setting/mode/zve10_net      content <- confirms we read the right partition
"""
import struct
import sys
from pathlib import Path

DEFAULT_IMAGES = [
    '/root/fw/zve10_backup/nflasha2.img',
    '/root/fw/zve10_backup/nflasha2_current.img',
]
WANT = [
    ('MODE', 'DMODE', 'gate for set_oreore_env'),
    ('', 'ENVTXT', 'value exported to im.elf'),
    ('MODE', 'PRELOAD', 'bootin debug preload path'),
]


class Fat:
    def __init__(self, data):
        self.d = data
        bps = struct.unpack_from('<H', data, 0x0B)[0]
        spc = data[0x0D]
        resv = struct.unpack_from('<H', data, 0x0E)[0]
        nfats = data[0x10]
        rootent = struct.unpack_from('<H', data, 0x11)[0]
        fatsz = struct.unpack_from('<H', data, 0x16)[0]
        self.bps, self.spc = bps, spc
        self.root_off = (resv + nfats * fatsz) * bps
        self.data_off = self.root_off + rootent * 32
        self.rootent = rootent

    def cluster_off(self, cl):
        return self.data_off + (cl - 2) * self.spc * self.bps

    def entries(self, cl=None):
        """Yield (name, ext, attr, cluster, size) for a directory."""
        off = self.root_off if cl is None else self.cluster_off(cl)
        n = self.rootent if cl is None else 512
        lfn = b''
        for i in range(n):
            e = self.d[off + i * 32: off + i * 32 + 32]
            if len(e) < 32 or e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = b''
                continue
            if e[11] == 0x0F:
                lfn = (e[1:11] + e[14:26] + e[28:32]) + lfn
                continue
            name = e[0:8].decode('latin1').rstrip()
            ext = e[8:11].decode('latin1').rstrip()
            cl_ = struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            long_name = ''
            if lfn:
                long_name = lfn.decode('utf-16-le', 'replace').split('\0')[0]
            lfn = b''
            yield name, ext, e[11], cl_, size, long_name

    def lookup(self, parts):
        """Resolve a path like ['MODE','DMODE'] to an entry tuple."""
        cl = None
        entry = None
        for i, want in enumerate(parts):
            found = None
            for name, ext, attr, cl_, size, long_name in self.entries(cl):
                cand = (name + ext).upper().replace('~1', '')
                if name.upper() == want or long_name.upper().replace('.', '') == want:
                    found = (name, ext, attr, cl_, size, long_name)
                    break
                if cand.startswith(want):
                    found = (name, ext, attr, cl_, size, long_name)
                    break
            if found is None:
                return None
            entry = found
            cl = found[3]
        return entry

    def read(self, entry):
        _, _, _, cl, size, _ = entry
        if size == 0:
            return b''
        return self.d[self.cluster_off(cl): self.cluster_off(cl) + size]


def main():
    images = sys.argv[1:] or DEFAULT_IMAGES
    bad = 0
    for path in images:
        p = Path(path)
        if not p.is_file():
            print('SKIP %s (missing)' % path)
            continue
        fat = Fat(p.read_bytes())
        print('== %s ==' % path)
        for dirname, fname, why in WANT:
            parts = ([dirname] if dirname else []) + [fname]
            entry = fat.lookup(parts)
            label = '/setting/' + ('mode/' if dirname else '') + fname.lower()
            if entry:
                print('  PRESENT %-30s %s' % (label, why))
            else:
                print('  MISSING %-30s %s' % (label, why))
        net = fat.lookup(['MODE', 'ZVE10NET']) or fat.lookup(['MODE', 'ZVE10_~1'])
        if net:
            print('  probe   /setting/mode/zve10_net   = %r'
                  % fat.read(net))
        else:
            print('  probe   /setting/mode/zve10_net   = not found')
            bad += 1
        print()
    if bad:
        print('FAIL: could not read the probe file, wrong partition or layout')
        raise SystemExit(1)
    print('OK: every image inspected; the gate file is reported above per image')


if __name__ == '__main__':
    main()

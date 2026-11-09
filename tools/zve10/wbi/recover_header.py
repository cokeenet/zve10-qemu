#!/usr/bin/env python3
"""Restore nflasha5 sector 0 from the archived pre-write copy.

Adapted from zve10-fb/restore_wbi.py. The upstream version requires bytes 0..511
to be entirely zero (the __wbi_drop signature). This camera's damage is different:
bytes 0..3 hold a marker we wrote (WBI1 -> invl), so the gate here instead
requires bytes 0..3 to equal the known-bad marker and bytes 4..4095 to match the
archive byte-for-byte. That proves the damage is exactly the four-byte edit.

Gates, in order:
  1. read 8 blocks at nflasha5
  2. require bytes 0..3 == BAD_MARKER
  3. require bytes 4..4095 byte-identical to the archive
  4. write back the archive's 512-byte header
  5. read back and require byte-exact match
No reboot is performed.
"""
import argparse
import struct
import sys
from pathlib import Path

VID, PID = 0x18D1, 0x4EE0
CHUNK = 4096
NFLASHA5_LBA = 0x05800000 // 512
BAD_MARKER = b'invl'

ARCHIVE = (Path(__file__).resolve().parents[2]
           / 'backups/wbi-A-applied-20261109/live-block0-4096.bin')


class Device:
    def __init__(self, dev):
        import usb.util
        self._u = usb.util
        self.dev = dev
        cfg = dev.get_active_configuration()
        self.intf = cfg[(0, 0)]
        usb.util.claim_interface(dev, self.intf)
        self.ep_out = usb.util.find_descriptor(
            self.intf, custom_match=lambda e: e.bEndpointAddress == 0x01)
        self.ep_in = usb.util.find_descriptor(
            self.intf, custom_match=lambda e: e.bEndpointAddress == 0x81)
        if self.ep_out is None or self.ep_in is None:
            raise RuntimeError('bulk endpoints 01/81 missing')

    def close(self):
        try:
            self._u.release_interface(self.dev, self.intf)
        except Exception:
            pass
        self._u.dispose_resources(self.dev)

    def read_reply(self, timeout=20000):
        while True:
            raw = bytes(self.ep_in.read(64, timeout=timeout))
            if len(raw) < 4:
                raise RuntimeError('short fastboot reply')
            tag, body = raw[:4], raw[4:]
            if tag == b'INFO':
                print('  INFO ' + body.decode('ascii', 'replace'))
                continue
            return tag, body

    def write(self, data):
        if self.ep_out.write(data, timeout=20000) != len(data):
            raise RuntimeError('short USB write')

    def command(self, text):
        self.write(text.encode('ascii'))
        return self.read_reply()

    def require(self, text):
        tag, body = self.command(text)
        if tag != b'OKAY':
            raise RuntimeError('%s -> %r %r' % (text, tag, body))
        return body

    def download(self, blob):
        tag, body = self.command('download:%08x' % len(blob))
        if tag != b'DATA':
            raise RuntimeError('download refused: %r %r' % (tag, body))
        for off in range(0, len(blob), CHUNK):
            self.write(blob[off:off + CHUNK])
        tag, body = self.read_reply()
        if tag != b'OKAY':
            raise RuntimeError('download tail: %r %r' % (tag, body))

    def read_blocks(self, lba, count):
        self.write(('oem upload:%08x:%08x' % (lba, count)).encode('ascii'))
        tag, body = self.read_reply()
        if tag != b'DATA':
            raise RuntimeError('upload refused: %r %r' % (tag, body))
        announced = int(body, 16)
        want = count * 512
        if announced != want:
            raise RuntimeError('announced %d, expected %d' % (announced, want))
        data = bytearray()
        while len(data) < want:
            n = min(0x10000, want - len(data))
            chunk = bytes(self.ep_in.read(n, timeout=20000))
            if not chunk:
                raise RuntimeError('empty block read')
            data.extend(chunk)
        tag, body = self.read_reply()
        if tag != b'OKAY':
            raise RuntimeError('upload tail: %r %r' % (tag, body))
        return bytes(data)


def hexdump(data, base=0, limit=96):
    for off in range(0, min(len(data), limit), 16):
        c = data[off:off + 16]
        print('%08x  %-47s  |%s|' % (
            base + off,
            ' '.join('%02x' % b for b in c),
            ''.join(chr(b) if 32 <= b < 127 else '.' for b in c)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--archive', default=str(ARCHIVE))
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    good = Path(a.archive).read_bytes()
    if len(good) < 4096:
        raise SystemExit('archive too small: %d' % len(good))
    header, good_rest = good[:512], good[512:4096]
    print('archive magic     : %r' % header[:4])
    print('archive non-zero  : %d/512' % sum(1 for b in header if b))

    import usb.core
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        print('REFUSE: fastboot device absent (inject stage1 first)')
        return 2
    d = Device(dev)
    try:
        print('stage2: %s' % d.require(
            'getvar:version-bootloader').decode('ascii', 'replace'))
        print('\nreading 8 blocks at lba %d ...' % NFLASHA5_LBA, flush=True)
        cur = d.read_blocks(NFLASHA5_LBA, 8)

        if cur[:4] != BAD_MARKER:
            print('REFUSE: bytes 0..3 = %r, expected %r'
                  % (cur[:4], BAD_MARKER))
            print('(if these are all zero, use zve10-fb/restore_wbi.py)')
            hexdump(cur)
            return 3
        print('gate 1 OK: bytes 0..3 hold the applied marker %r' % cur[:4])

        if cur[4:4096] != good_rest:
            i = next(k for k in range(len(good_rest)) if cur[4 + k] != good_rest[k])
            print('REFUSE: body differs at +%d' % (4 + i))
            hexdump(cur)
            print('--- expected ---')
            hexdump(good)
            return 4
        print('gate 2 OK: bytes 4..4095 match the archive byte-for-byte')

        if a.dry_run:
            print('\ndry-run: would write 512 bytes at lba %d' % NFLASHA5_LBA)
            return 0

        print('\nwriting 512-byte header ...', flush=True)
        d.download(header)
        d.require('flash:nflasha5')

        print('verifying ...', flush=True)
        after = d.read_blocks(NFLASHA5_LBA, 1)
        if after != header:
            print('FAIL: readback mismatch')
            hexdump(after)
            return 5
        print('\nSUCCESS: header restored byte-exact')
        hexdump(after[:64])
        v = struct.unpack_from('<4sIIIIIIII', after, 0)
        print('\n  magic        : %r' % v[0])
        print('  sections     : %d' % v[1])
        print('  flag         : 0x%02x' % v[2])
        print('  resume_vector: 0x%08x' % v[3])
        print('  sector_size  : %d' % v[5])
        print('  r_data_size  : 0x%08x' % v[6])
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())

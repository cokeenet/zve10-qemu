#!/usr/bin/env python3
"""Restore nflasha5 sector 0 after a Sony ud_wbi_drop invalidation.

Gate mirrors zve10-fb/restore_wbi.py: bytes 0..511 must be entirely zero (the
signature of a successful Sony drop), and bytes 512..4095 must already match the
archive. Then the archived 512-byte header is written and read back.

No reboot.
"""
import argparse
import struct
import sys
from pathlib import Path

VID, PID = 0x18D1, 0x4EE0
CHUNK = 4096
NFLASHA5_LBA = 0x05800000 // 512
ARCHIVE = (Path(__file__).resolve().parents[1]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--archive', default=str(ARCHIVE))
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    good = Path(a.archive).read_bytes()
    if len(good) < 4096:
        raise SystemExit('archive too small')
    header = good[:512]
    good_rest = good[512:4096]
    print('archive magic    : %r' % header[:4])
    print('archive non-zero : %d/512' % sum(1 for b in header if b))

    import usb.core
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        print('REFUSE: fastboot device absent')
        return 2
    d = Device(dev)
    try:
        print('stage2: %s' % d.require(
            'getvar:version-bootloader').decode('ascii', 'replace'))
        print('reading 8 blocks at lba %d ...' % NFLASHA5_LBA, flush=True)
        cur = d.read_blocks(NFLASHA5_LBA, 8)

        if any(cur[:512]):
            i = next(k for k in range(512) if cur[k])
            print('REFUSE: byte %d of the header is non-zero; not a clean drop'
                  % i)
            print('  first 64 bytes: %s' % cur[:64].hex())
            return 3
        print('gate 1 OK: bytes 0..511 are entirely zero')

        if cur[512:4096] != good_rest:
            i = next(k for k in range(len(good_rest))
                     if cur[512 + k] != good_rest[k])
            print('REFUSE: body differs at +%d' % (512 + i))
            return 4
        print('gate 2 OK: bytes 512..4095 match the archive')

        if a.dry_run:
            print('dry-run: would write 512 bytes at lba %d' % NFLASHA5_LBA)
            return 0

        print('writing 512-byte header ...', flush=True)
        d.download(header)
        d.require('flash:nflasha5')
        after = d.read_blocks(NFLASHA5_LBA, 1)
        if after != header:
            print('FAIL: readback mismatch')
            print('  got: %s' % after[:64].hex())
            return 5
        print('SUCCESS: header restored byte-exact')
        v = struct.unpack_from('<4sIIIIIIII', after, 0)
        print('  magic=%r sections=%d flag=0x%02x sector_size=%d r_data_size=0x%08x'
              % (v[0], v[1], v[2], v[5], v[6]))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())

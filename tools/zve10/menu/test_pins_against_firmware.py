#!/usr/bin/env python3
"""Verify every hard-coded firmware anchor against the real libraries.

The menu library patches native code by absolute offset, so every constant in it
is a claim about specific firmware bytes. A wrong claim either fails the 12-byte
memcmp (hook silently skipped) or, worse, matches somewhere unintended.

This check reads the pins straight out of the shipped source and compares them
with the reference libraries, so a typo in a constant fails here rather than on
the camera.

Requires the reference tree at /root/fw/ref_e10 (the same MD5s the hardware
report used). Skipped, not passed, when that tree is absent.
"""
import re
from pathlib import Path

REF = Path('/root/fw/ref_e10/usr/lib')
ROOT = Path(__file__).resolve().parent
PINS = ROOT / 'native_pins.h'
INC = ROOT / 'native_entry.inc'

EXPECTED_MD5 = {
    'libObj.so': '12cf982dd3eaded426b0b9c703a681cd',
    'viewUnified2.so': 'edb07179859e2fb66f59df4b7023cbbb',
}

# name -> (library, offset)
TARGETS = [
    ('ne_pin_init', 'CautionConfig.so', 0xb8dffa),
    ('ne_pin_populate', 'viewUnified2.so', 0x3014f0),
    ('ne_pin_main', 'viewUnified2.so', 0x2fccc0),
    ('ne_pin_row', 'viewUnified2.so', 0x862130),
]


def load_pins():
    text = PINS.read_text()
    out = {}
    for m in re.finditer(
            r'static const unsigned char (ne_pin_\w+)\[12\]\s*=\s*\{([^}]*)\}',
            text):
        out[m.group(1)] = bytes(int(x) for x in re.findall(r'\d+', m.group(2)))
    return out


def main():
    if not REF.is_dir():
        raise SystemExit('SKIP: reference tree %s not present' % REF)

    import hashlib
    problems = []
    blobs = {}
    for name, md5 in EXPECTED_MD5.items():
        p = REF / name
        if not p.is_file():
            problems.append('%s missing' % name)
            continue
        got = hashlib.md5(p.read_bytes()).hexdigest()
        if got != md5:
            problems.append('%s md5 %s != %s' % (name, got, md5))
        blobs[name] = p.read_bytes()
    caution_path = REF / 'CautionConfig.so'
    if caution_path.is_file():
        blobs['CautionConfig.so'] = caution_path.read_bytes()
    else:
        problems.append('CautionConfig.so missing')

    pins = load_pins()
    if len(pins) != 4:
        problems.append('expected 4 pins in native_pins.h, found %d' % len(pins))

    for name, lib, off in TARGETS:
        want = pins.get(name)
        blob = blobs.get(lib)
        if want is None:
            problems.append('%s not defined' % name)
            continue
        if blob is None:
            continue
        if len(want) != 12:
            problems.append('%s is %d bytes, not 12' % (name, len(want)))
            continue
        got = blob[off:off + 12]
        if got != want:
            problems.append('%s: %s+0x%X want %s got %s'
                            % (name, lib, off, want.hex(), got.hex()))

    # The caution constructor anchor used by nm_try_install must be 0xb8eb21 or
    # 0xb8eb20 (with/without the Thumb bit).
    inc = INC.read_text()
    src = (ROOT / 'mini_main.c').read_text()
    if '0xb8eb21' not in src or '0xb8eb20' not in src:
        problems.append('caution anchor 0xb8eb21/0xb8eb20 missing from install path')

    # The insertion gate must be the Thumb return address after BLX r4 at
    # 0x30191e, i.e. 0x301921.
    if 'views_base + 0x301921' not in inc:
        problems.append('insertion gate is not views_base + 0x301921')
    views = blobs.get('viewUnified2.so')
    if views:
        blx = views[0x30191e:0x301920]
        if blx.hex() != 'a047':
            problems.append('expected BLX r4 (a047) at 0x30191e, got %s'
                            % blx.hex())

    if problems:
        print('FAIL:')
        for p in problems:
            print('  - ' + p)
        raise SystemExit(1)

    print('PASS: all four 12-byte pins match the reference firmware byte-for-byte '
          '(%s inside CautionConfig.so, three inside viewUnified2.so); the caution '
          'ctor anchor is 0xb8eb21; and the insertion gate is the Thumb return '
          'address 0x301921 after the BLX r4 at 0x30191e'
          % 'ne_pin_init')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Verify every raw viewUnified2 offset the library calls into is a real entry.

The library reaches into viewUnified2.so by absolute offset. Only three of those
offsets are protected by a 12-byte pin; the rest are called directly with no
verification at all:

    0x2F4B5B  0x2F5999  0x2F5F23  0x2F5F65  0x2FFD75  0x861C29  0x861EE7

A wrong constant there means calling into the middle of an instruction the first
time a row exists or the widget is bound, which is exactly the kind of fault that
looks like "the camera died" with no USB. So each one is checked against the
firmware: stripping the Thumb bit must land on a function prologue.

Two offsets are expected NOT to be prologues and are allow-listed:
  0x301921  the insertion gate, which is deliberately a mid-function return
            address after the BLX r4 at 0x30191e
  0xBAABB0  the menu view vptr, which is a vtable pointer, not code
"""
import re
from pathlib import Path

REF = Path('/root/fw/ref_e10/usr/lib/viewUnified2.so')
ROOT = Path(__file__).resolve().parent
INC = ROOT / 'native_entry.inc'

DATUM = {0x301921, 0xBAABB0}          # not code: return addr and vtable ptr


def is_prologue(data, entry):
    if entry + 4 > len(data):
        return False
    hw = data[entry] | (data[entry + 1] << 8)
    if (hw & 0xFF00) == 0xB500:       # Thumb push {…, lr}
        return True
    if data[entry:entry + 2] == b'\x2d\xe9':   # stmdb sp!, {…}
        return True
    return False


def main():
    if not REF.is_file():
        raise SystemExit('SKIP: %s not present' % REF)

    data = REF.read_bytes()
    inc = INC.read_text()
    offsets = sorted({int(x, 16)
                      for x in re.findall(r'views_base \+ (0x[0-9a-f]+)', inc)})
    if not offsets:
        raise SystemExit('FAIL: no views_base offsets found in native_entry.inc')

    problems = []
    entries = 0
    for off in offsets:
        if off in DATUM:
            continue
        entry = off if off % 2 == 0 else off - 1
        if not is_prologue(data, entry):
            problems.append('0x%06X (entry 0x%06X) is not a function prologue: %s'
                            % (off, entry, data[entry:entry + 4].hex()))
        else:
            entries += 1

    # The insertion gate must stay the Thumb return address after BLX r4.
    if 'views_base + 0x301921' not in inc:
        problems.append('insertion gate is not 0x301921')
    if data[0x30191e:0x301920].hex() != 'a047':
        problems.append('0x30191e is not BLX r4 (a047): %s'
                        % data[0x30191e:0x301920].hex())

    if problems:
        print('FAIL:')
        for p in problems:
            print('  - ' + p)
        raise SystemExit(1)

    print('PASS: all %d callable viewUnified2 offsets resolve to function '
          'prologues once the Thumb bit is stripped; the two non-code offsets '
          '(0x301921 return address, 0xBAABB0 vptr) stay allow-listed, and the '
          'BLX r4 at 0x30191e that makes the gate correct is intact' % entries)


if __name__ == '__main__':
    main()

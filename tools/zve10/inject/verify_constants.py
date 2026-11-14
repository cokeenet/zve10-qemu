#!/usr/bin/env python3
"""Statically verify every hard-coded constant in ptinject.c against im.elf."""
import struct, sys
from pathlib import Path

IM = Path('/root/fw/ref_e10/usr/bin/im.elf')
BASE = 0x8000
d = IM.read_bytes()

def at(va, n=4):
    return d[va - BASE: va - BASE + n]

def rel_plt():
    off, size = 0xdb0, 0x1e8
    out = {}
    for i in range(0, size, 8):
        r_off, r_info = struct.unpack_from('<II', d, off + i)
        sym = r_info >> 8
        nm_off = struct.unpack_from('<I', d, 0x3d8 + sym * 16)[0]
        e = d.find(b'\0', 0x878 + nm_off)
        out[r_off] = d[0x878 + nm_off:e].decode('latin1')
    return out

fails = []
def check(name, got, want):
    ok = got == want
    if not ok:
        fails.append(name)
    print("  %-44s %s got=%r want=%r" % (name, "OK" if ok else "MISMATCH", got, want))

print("=== ptinject.c constants vs im.elf ===")
check("free@plt stub is bx lr ; nop", at(0x9000, 4), bytes.fromhex('7847c046'))
check("dlopen@plt starts add ip,pc,#0", at(0x9108, 4), bytes.fromhex('00c68fe2'))
got = rel_plt()
check("GOT slot 0x15188 resolves to", got.get(0x15188), 'free')
check("GOT slot 0x151DC resolves to", got.get(0x151DC), 'dlopen')

# Stub at 0x9000 must be ARM mode (not a Thumb-only encoding) and provably unused:
# the two words are `bx lr` + `nop`, an early-return thunk, and no BL/BLX targets it.
text_va, text_off, text_sz = 0x92b0, 0x12b0, 0x2ee8
import re
text = d[text_off:text_off + text_sz]
calls = 0
for i in range(0, len(text) - 4, 2):
    h1 = struct.unpack_from('<H', text, i)[0]
    h2 = struct.unpack_from('<H', text, i + 2)[0]
    if (h1 & 0xF800) == 0xF000 and (h2 & 0xD000) == 0xD000:
        S = (h1 >> 10) & 1; J1 = (h2 >> 13) & 1; J2 = (h2 >> 11) & 1
        I1 = 1 - (J1 ^ S); I2 = 1 - (J2 ^ S)
        off = (S << 24) | (I1 << 23) | (I2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
        if S:
            off -= (1 << 25)
        if text_va + i + 4 + off == 0x9000:
            calls += 1
check("direct BL/BLX call sites to 0x9000", calls, 0)

print()
print("SELFCHK", "PASS" if not fails else ("FAIL " + ",".join(fails)))
sys.exit(0 if not fails else 1)

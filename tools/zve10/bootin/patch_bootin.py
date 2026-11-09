#!/usr/bin/env python3
"""给 bootin.elf 打补丁：把 putenv("LD_PRELOAD") 改成 putenv("LD_PRELOAD=/etc/nm.so")

原理：
  bootin 在 dmode 含 'p' 时执行 putenv("LD_PRELOAD")，glibc 会删除该键。
  把它改成 putenv("LD_PRELOAD=/etc/nm.so")，im.elf 就会继承这个值。

改动（三处）：
  ① LOAD#1 filesz/memsz: 0x2CEC → 0x3000（扩大 788 字节，原区域全零）
  ② vaddr 0xACEC（文件偏移 0x2CEC）写 "LD_PRELOAD=/etc/nm.so\0"（22 B）
  ③ 0x9BB0 处字面量: 0xA778 → 0xACEC（putenv 参数改指向新串）
"""
import struct, sys, hashlib
from pathlib import Path

SRC = '/root/fw/ref_e10/usr/bin/bootin.elf'
DST = '/tmp/bootin.patched'

NEW_STR = b"LD_PRELOAD=/etc/nm.so\x00"   # 22 B
STR_VADDR = 0xACEC
STR_OFF   = STR_VADDR - 0x8000            # 0x2CEC
LIT_ADDR  = 0x9BB0
LIT_OFF   = LIT_ADDR - 0x8000             # 0x1BB0
OLD_VAL   = 0xA778

d = bytearray(Path(SRC).read_bytes())
orig = bytes(d)
print("原文件 %d B, md5=%s" % (len(d), hashlib.md5(d).hexdigest()))

# --- 校验前提 ---
assert struct.unpack_from('<I', d, LIT_OFF)[0] == OLD_VAL, "字面量不匹配"
gap = d[STR_OFF:0x3000]
assert all(b == 0 for b in gap), "空隙非全零"
print("✓ 前提校验通过：字面量=0x%X，空隙 %d B 全零" % (OLD_VAL, len(gap)))

# --- ① 扩 LOAD#1 ---
e_phoff = struct.unpack_from('<I', d, 28)[0]
e_phnum = struct.unpack_from('<H', d, 44)[0]
ph1 = None
for i in range(e_phnum):
    o = e_phoff + i*32
    p_type = struct.unpack_from('<I', d, o)[0]
    p_off  = struct.unpack_from('<I', d, o+4)[0]
    p_fsz  = struct.unpack_from('<I', d, o+16)[0]
    if p_type == 1 and p_off == 0:
        ph1 = o
        print("  LOAD#1 在程序头偏移 0x%X, filesz=0x%X" % (o, p_fsz))
        break
assert ph1 is not None
struct.pack_into('<I', d, ph1+16, 0x3000)   # p_filesz
struct.pack_into('<I', d, ph1+20, 0x3000)   # p_memsz

# --- ② 写字符串 ---
d[STR_OFF:STR_OFF+len(NEW_STR)] = NEW_STR
print("  字符串 @vaddr 0x%X: %r" % (STR_VADDR, NEW_STR))

# --- ③ 改字面量 ---
struct.pack_into('<I', d, LIT_OFF, STR_VADDR)
print("  字面量 0x%X: 0x%X → 0x%X" % (LIT_ADDR, OLD_VAL, STR_VADDR))

Path(DST).write_bytes(d)
print("\n✓ 写出 %s (%d B), md5=%s" % (DST, len(d), hashlib.md5(d).hexdigest()))

# --- 验证 ---
d2 = Path(DST).read_bytes()
print("\n验证:")
print("  LOAD#1 filesz=0x%X memsz=0x%X" % (
    struct.unpack_from('<I', d2, ph1+16)[0],
    struct.unpack_from('<I', d2, ph1+20)[0]))
print("  字面量 = 0x%X" % struct.unpack_from('<I', d2, LIT_OFF)[0])
print("  字符串 = %r" % d2[STR_OFF:STR_OFF+24])
diff = [i for i in range(len(d2)) if d2[i] != orig[i]]
print("  差异: %d 字节" % len(diff))
print("  差异位置: %s" % ', '.join('0x%X'%(i+0x8000) for i in diff[:25]))

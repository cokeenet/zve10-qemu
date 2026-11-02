#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""三道 magic 全放行 + count 归零（libObj 装载器 0x206d04）。

根因链：Backup_read 失败 → buf 全垃圾 → 三道 magic 检查逐一失败：
  ① BIN!  @0x206d54  → DISCRIM 已放行
  ② CNF!  @0x206d82  → 本补丁放行
  ③ PTN!  @0x206e9c  → 本补丁放行（循环后检查 buf[20]，失败码 r4=-9）
count_A/count_B 归零保证空列表路径安全。

补丁表：
  0x206d72  ldr r6,[r5,#8]   -> movs r6,#0     (count_A=0)
  0x206d78  ldr r3,[r5,#12]  -> movs r3,#0     (count_B=0)
  0x206d82  beq.n 206d9e     -> b.n 206d9e     (CNF! 放行)
  0x206e9c  beq.n 206eb8     -> b.n 206eb8     (PTN! 放行)
"""
import sys

PATCHES = [
    (0x206d72, bytes.fromhex('0026'), 'count_A=0'),
    (0x206d78, bytes.fromhex('0023'), 'count_B=0'),
    (0x206d82, bytes.fromhex('0ce0'), 'CNF! 放行'),
    (0x206e9c, bytes.fromhex('0ce0'), 'PTN! 放行'),
]

def main() -> int:
    p = sys.argv[1]
    d = bytearray(open(p, 'rb').read())
    for off, new, desc in PATCHES:
        cur = bytes(d[off:off+len(new)])
        if cur == new:
            print('  0x%04x 已是 %s (%s)' % (off, new.hex(' '), desc))
        else:
            d[off:off+len(new)] = new
            print('  0x%04x %s -> %s (%s)' % (off, cur.hex(' '), new.hex(' '), desc))
    for off, new, desc in PATCHES:
        v = bytes(d[off:off+len(new)])
        assert v == new, '回读失败 @0x%04x: %s' % (off, v.hex())
    open(p, 'wb').write(d)
    print('回读 OK (%d 处)' % len(PATCHES))
    return 0

if __name__ == '__main__':
    sys.exit(main())

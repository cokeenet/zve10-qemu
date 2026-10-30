#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_libobj_dtor_zero.py — 修 CInfraIndctrConf 析构的未初始化字段 free

根因（Kimi 第三轮 §1）：析构 free 六个字段，构造只初始化四个。
  this+0x0C 和 this+0x10 是裸 new(44) 留下的堆垃圾（前世块旧指针），
  NULL 检查挡不住非零垃圾 → delete[](垃圾) → free 读野指针 chunk 头 → 崩。

修法：把析构里加载这两个字段的 ldr 换成 movs r0, #0（2 字节），让 NULL 检查跳过。
  0x206c8e: ldr r0, [r4, #12]  →  movs r0, #0  (0x2000)
  0x206c96: ldr r0, [r4, #16]  →  movs r0, #0  (0x2000)
"""
import sys

def main() -> int:
    path = sys.argv[1]
    d = bytearray(open(path, 'rb').read())

    patches = [
        (0x206C8E, bytes.fromhex('e068'), bytes.fromhex('0020'),
         'ldr r0,[r4,#12] -> movs r0,#0 (this+0x0C)'),
        (0x206C96, bytes.fromhex('2069'), bytes.fromhex('0020'),
         'ldr r0,[r4,#16] -> movs r0,#0 (this+0x10)'),
    ]

    for off, orig, new, desc in patches:
        cur = bytes(d[off:off+2])
        if cur == new:
            print('  0x%05x 已是 movs r0,#0 (%s)' % (off, desc))
        elif cur == orig:
            d[off:off+2] = new
            print('  0x%05x %s -> %s (%s)' % (off, cur.hex(' '), new.hex(' '), desc))
        else:
            print('  0x%05x 指纹不符: %s (期望 %s) — 跳过' % (off, cur.hex(), orig.hex()))
            continue

    for off, orig, new, desc in patches:
        v = bytes(d[off:off+2])
        if v == new:
            pass
    open(path, 'wb').write(d)
    print('done')
    return 0

if __name__ == '__main__':
    sys.exit(main())

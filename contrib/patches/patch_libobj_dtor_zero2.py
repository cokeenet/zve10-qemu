#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_libobj_dtor_zero2.py — 兄弟类析构的同款垃圾字段置零（Kimi 第四轮 §Q1）

根因：0x205dd0 的销毁器调用 0x205d84 的循环析构，每个记录 free record+0x0C 和 record+0x10。
这些字段在该类的 ctor 里没被初始化（同 Conf 的 bug 家族）→ free 堆垃圾 → 崩。

修法（与 Conf 同款）：在循环里把加载这两个字段的 ldr 置零，让 NULL 检查跳过 free。
  0x205d94: ldr r0, [r0, #12]    ->  movs r0, #0     (2B)
  0x205da0: ldr.w r8, [r3, #16]  ->  mov.w r8, #0    (4B)
"""
import sys

def main() -> int:
    path = sys.argv[1]
    d = bytearray(open(path, 'rb').read())

    patches = [
        (0x205D94, bytes.fromhex('c068'), bytes.fromhex('0020'),
         'ldr r0,[r0,#12] -> movs r0,#0'),
        (0x205DA0, bytes.fromhex('d3f81080'), bytes.fromhex('4ff00008'),
         'ldr.w r8,[r3,#16] -> mov.w r8,#0'),
    ]

    for off, orig, new, desc in patches:
        cur = bytes(d[off:off+len(new)])
        if cur == new:
            print('  0x%05x 已是 %s (%s)' % (off, new.hex(' '), desc))
        elif cur == orig:
            d[off:off+len(new)] = new
            v = bytes(d[off:off+len(new)])
            assert v == new
            print('  0x%05x %s -> %s (%s)  OK' % (off, cur.hex(' '), new.hex(' '), desc))
        else:
            print('  0x%05x 指纹不符: %s (期望 %s) - 跳过' % (off, cur.hex(), orig.hex()))

    open(path, 'wb').write(d)
    print('done')
    return 0

if __name__ == '__main__':
    sys.exit(main())

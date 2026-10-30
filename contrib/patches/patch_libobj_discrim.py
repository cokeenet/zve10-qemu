#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""判别补丁（libObj 装载器 0x206d04）：
  ① 0x206d2c blx Backup_get_datasize -> movs r0,#28（强制 size=28，跳过 get 查询）
  ② 0x206d54 bne.w(magic1 失败分支)  -> 双 nop（放行第一道 magic）
保留 magic2("CNF!") 检查：buf 若被 Backup_read 正确填充 -> 过；buf 是垃圾 -> 崩点会变。

★ 2026-10-27 字节序修正（Kimi 第五轮答复）：此前写成 bf00bf00，文件字节序下
  半字 0x00bf = `lsls r7,r7,#2`，把装载器帧指针 r7 左移 4 位，退出弹栈时
  SP 落进 libObj r-x 区、PC 变成字符串（"rion" 崩点）。正确 NOP 文件字节是 00 bf。
"""
import sys

# (offset, expected_orig, new, desc)  —— 指纹/新字节都是**文件字节序**（半字小端存储）
# objdump 显示的半字值需逐半字反转才是文件字节：f634 ed14 -> 34f614ed
PATCHES = [
    (0x206d2c, '34f614ed', '1c2000bf', 'blx get_datasize -> movs r0,#28 ; nop'),
    (0x206d54, '40f06681', '00bf00bf', 'bne.w(BIN! fail) -> nop ; nop  [小端修正]'),
]

def main() -> int:
    p = sys.argv[1]
    d = bytearray(open(p, 'rb').read())
    for off, exp, new, desc in PATCHES:
        new_b = bytes.fromhex(new)
        cur = bytes(d[off:off+4])
        if cur == new_b:
            print('  0x%06x 已打过 (%s)' % (off, desc))
            continue
        if cur.hex() != exp:
            print('!! 0x%06x 指纹不符: got %s expect %s —— 树版本变了，拒绝打' %
                  (off, cur.hex(), exp))
            return 1
        d[off:off+4] = new_b
        print('  0x%06x  %s -> %s (%s)' % (off, cur.hex(), new, desc))
    for off, exp, new, desc in PATCHES:
        v = bytes(d[off:off+4])
        assert v == bytes.fromhex(new), '回读失败 @0x%06x: %s' % (off, v.hex())
    open(p, 'wb').write(d)
    print('回读 OK (%d 处)' % len(PATCHES))
    return 0

if __name__ == '__main__':
    sys.exit(main())

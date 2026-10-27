#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_stream_pfn.py — 把 stream_mmap 里「清除 VM_PFNMAP|VM_IO」的指令改成 NOP

背景（2026-10-26）：
  stream.ko / stream2.ko 的 stream_mmap() 在 remap_pfn_range() 成功后，
  执行 `bic r3, r3, #0x4400` 清除 VMA->vm_flags 的 VM_PFNMAP(0x400) + VM_IO(0x4000)，
  导致 munmap 时 vm_normal_page() 返回 struct page → page_remove_rmap() 打穿 mapcount
  → Bad page map（60 条）→ 下游 free() permission fault / 间歇崩溃。
  本补丁把该 bic 指令替换为 ARM NOP，保留 VM_PFNMAP|VM_IO。

补丁点（文件偏移 = .text 文件偏移 0x34 + vaddr）：
  stream.ko   stream_mmap  vaddr 0x58  file 0x8C
  stream2.ko  stream_mmap  vaddr 0x260 file 0x294
指纹（文件小端序）：e3c33b11 -> 11 3b c3 e3
NOP（ARM）：      e320f000 -> 00 f0 20 e3
"""
import sys
import struct
from pathlib import Path

ORIG = bytes.fromhex('113bc3e3')   # bic r3, r3, #0x4400（文件小端序）
NEW  = bytes.fromhex('00f020e3')   # nop（ARM e320f000，文件小端序）

# (文件名, .text 文件偏移, vaddr, 说明)
TARGETS = [
    ('stream.ko',  0x34, 0x58,  'stream_mmap 0x58'),
    ('stream2.ko', 0x34, 0x260, 'stream_mmap 0x260'),
]

def patch_one(path: Path, text_off: int, vaddr: int, desc: str) -> bool:
    d = bytearray(path.read_bytes())
    foff = text_off + vaddr
    got = bytes(d[foff:foff + 4])
    print('  [%s] %s  file=0x%03x  当前: %s' % (path.name, desc, foff, got.hex(' ')))
    if got == NEW:
        print('    已是 NOP，跳过')
        return True
    assert got == ORIG, '指纹不符 @0x%03x: %s（期望 %s）' % (foff, got.hex(), ORIG.hex())
    d[foff:foff + 4] = NEW
    # 回读校验
    verify = bytes(d[foff:foff + 4])
    assert verify == NEW, '回读失败: %s' % verify.hex()
    path.write_bytes(d)
    print('    -> nop（回读校验 OK）')
    return True

def main():
    if len(sys.argv) < 2:
        # 默认目录：解包树的 kmod/
        base = Path('/mnt/g/WorkBuddy/zve10/unpacked/zv1f/firmware.tar_unpacked/'
                    '0700_part_image/dev/nflasha15_unpacked/kmod')
    else:
        base = Path(sys.argv[1])
        if base.is_file():
            base = base.parent

    ok = True
    for fname, text_off, vaddr, desc in TARGETS:
        p = base / fname
        if not p.exists():
            print('  !! %s 不存在，跳过' % p)
            ok = False
            continue
        try:
            patch_one(p, text_off, vaddr, desc)
        except AssertionError as e:
            print('    !! %s' % e)
            ok = False
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()

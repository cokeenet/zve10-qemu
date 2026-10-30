#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 WBI 前三段挪到高地址，腾出 0x20001000 起 1.2MB 给真机 DB

为什么：
  实测内核（backup.ko）会把 DB 头 +0x00 写回 0x20001000（硬编码），
  所以 DB 必须落在 0x20001000，不能挪。
  但真机 DB 1.2MB，原来那里只有 128KB（WBI 段1 在 0x20021000）。

  WBI 段表现状：
    { 0x001000, 0x20021000 }   段1   4KB
    { 0x0EC000, 0x20028000 }   段2  944KB
    { 0x008000, 0x20145000 }   段3   32KB
    { 0x500000, 0x03085000 }   段4    5MB
    { 0x96D000, 0x11000000 }   段5  9.9MB
    { 0x2A48000, 0x20200000 }  段6   42MB
    { 0x200000, 0x23E00000 }   段7    2MB

  把段 1/2/3 挪到 0x25000000 起（段7 结束 0x24000000 之后、KLOG 0x30000000 之前的空闲区）：
    { 0x001000, 0x25000000 }
    { 0x0EC000, 0x25001000 }
    { 0x008000, 0x250ED000 }
  → 腾出 0x20001000 - 0x2014D000（约 1.33MB），真机 DB（1.2MB）放得下。
"""
import re
import sys
from pathlib import Path

C = Path('/root/qemu-bionz/hw/arm/bionz.c')
DB_ADDR = 0x20001000
MOVES = [
    (0x001000, 0x20021000, 0x25000000),
    (0x0EC000, 0x20028000, 0x25001000),
    (0x008000, 0x20145000, 0x250ED000),
]


def main():
    s = C.read_text(encoding='utf-8')

    # 1) 挪 WBI 段
    changed = 0
    for size, old, new in MOVES:
        pat = '{ 0x%06X, 0x%08X }' % (size, old)
        rep = '{ 0x%06X, 0x%08X }' % (size, new)
        if pat in s:
            s = s.replace(pat, rep, 1)
            print('  WBI 段 0x%08X -> 0x%08X (size 0x%X)' % (old, new, size))
            changed += 1
        elif rep in s:
            print('  WBI 段 0x%08X 已挪好' % new)
            changed += 1
        else:
            print('  !! 没找到 WBI 段 0x%08X' % old)

    # 2) DB 落点改回 0x20001000
    pat = re.compile(r'(rom_add_blob_fixed\("backup_db",\s*backup_db,\s*sizeof\(backup_db\),\s*)0x[0-9a-fA-F]+(\))')
    m = pat.search(s)
    if m:
        cur = int(m.group(0).split(',')[-1].strip().rstrip(')'), 16)
        if cur != DB_ADDR:
            s = s[:m.start()] + m.group(1) + ('0x%08x' % DB_ADDR) + m.group(2) + s[m.end():]
            print('  backup_db 落点 0x%08X -> 0x%08X' % (cur, DB_ADDR))
        else:
            print('  backup_db 落点已是 0x%08X' % DB_ADDR)
    else:
        print('  !! 没找到 backup_db 的 rom_add_blob_fixed')

    C.write_text(s, encoding='utf-8')
    print('\n完成。DB 落点 0x%08X，占 0x%08X 字节' % (DB_ADDR, 0x12DB1C))

    # 3) 同步 DB 文件头
    import struct
    for f in sys.argv[1:] or ['/mnt/g/WorkBuddy/zve10/work/backup_db.bin',
                              '/mnt/g/WorkBuddy/zve10/work/real_backup/Backup.bin']:
        p = Path(f)
        if not p.exists():
            print('  -- %s 不存在' % p); continue
        d = bytearray(p.read_bytes())
        cur = struct.unpack_from('<I', d, 0)[0]
        if cur != DB_ADDR:
            struct.pack_into('<I', d, 0, DB_ADDR)
            p.write_bytes(d)
            print('  %-40s +0x00 %#010x -> %#010x' % (p.name, cur, DB_ADDR))
        else:
            print('  %-40s +0x00 已是 %#010x' % (p.name, DB_ADDR))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""ZV-E10 5GHz Wi-Fi 解锁 —— CautionConfig.so 单字节。

依据 https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock
  ① /usr/lib/CautionConfig.so 文件内偏移 0xD778E4:  0x20 → 0x1B
  ② Backup 属性 0x01071C54 → 0x01
  ③ Backup 属性 0x00A5000B → 0x02
  ④ Backup 属性 0x00A5000C → 0x16

本工具只做 ①，走 fastboot。②③④ 需要相机正常启动后用上游 tools/wifi.py。

用法:
    python3 tools/zve10/fix/wifi5g.py --check
    python3 tools/zve10/fix/wifi5g.py --apply
"""
import argparse
import hashlib
import struct
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'dev'))
sys.path.insert(0, str(HERE.parent / 'fat'))

import fb      # noqa: E402
import ext2    # noqa: E402

PART = 'nflasha15'
FILE = '/lib/CautionConfig.so'
EXPECT_SIZE = 17_694_160
EXPECT_SHA = '5ad8746a229da259fe00b297e8f897d682f7ae9a13393025b3d1c95a808e68d7'
PATCHED_SHA = '14f42cc09ee1b9ddde5352482c13b68e5a5e97d9d79e8456087b5e0b7af6ff6a'
OFFSET = 0xD778E4
OLD = 0x20
NEW = 0x1B
BACKUP = HERE.parent / 'backups'


def file_block_map(fs, inode_n):
    """返回 ([(块号, 该块内字节范围)], 文件大小)。

    只处理直接块 + 单级 + 双级间接，足够覆盖 17 MB。
    """
    ino = fs.inode(inode_n)
    size = ino['size']
    per = fs.bs // 4
    blocks = []
    got = 0

    def add(b):
        nonlocal got
        n = min(fs.bs, size - got)
        if n <= 0:
            return False
        blocks.append((b, got, n))
        got += n
        return got < size

    for b in ino['block'][:12]:
        if not b or not add(b):
            break
    if got < size and ino['block'][12]:
        ptrs = fs._blk(ino['block'][12])
        for i in range(per):
            b = struct.unpack_from('<I', ptrs, i * 4)[0]
            if not b or not add(b):
                break
    if got < size and ino['block'][13]:
        l1 = fs._blk(ino['block'][13])
        done = False
        for i in range(per):
            ind = struct.unpack_from('<I', l1, i * 4)[0]
            if not ind:
                break
            ptrs = fs._blk(ind)
            for j in range(per):
                b = struct.unpack_from('<I', ptrs, j * 4)[0]
                if not b or not add(b):
                    done = True
                    break
            if done:
                break
    return blocks, size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    if not args.apply:
        args.check = True

    print('=== ZV-E10 5GHz 解锁：CautionConfig.so 单字节 ===')
    print('  文件: /usr%s' % FILE)
    print('  改动: 偏移 0x%X  0x%02X → 0x%02X' % (OFFSET, OLD, NEW))
    print()

    fs = ext2.Ext2(PART)
    ino = fs.lookup(FILE)
    meta = fs.inode(ino)
    print('  inode=%d  size=%d' % (ino, meta['size']))

    if meta['size'] != EXPECT_SIZE:
        print('  ✗ 大小不符！期望 %d' % EXPECT_SIZE)
        return 2

    # 定位目标字节所在的文件块
    blocks, size = file_block_map(fs, ino)
    tgt = None
    for b, start, n in blocks:
        if start <= OFFSET < start + n:
            tgt = (b, OFFSET - start)
            break
    if tgt is None:
        print('  ✗ 偏移 0x%X 不在文件范围内' % OFFSET)
        return 2
    blkno, within = tgt
    print('  目标落在文件块 %d（块内偏移 0x%X），分区偏移 0x%X' % (blkno, within, blkno * fs.bs + within))

    # 算完整文件 SHA-256（只校验大小与目标字节附近，全量太慢）
    print('\n  校验目标字节上下文 …')
    part_off = blkno * fs.bs + within
    cur = fb.read_part_range(PART, part_off, 1)
    ctx = fb.read_part_range(PART, part_off - 16, 48)
    print('  当前字节: 0x%02X' % cur[0])
    print('  上下文  : %s' % ctx.hex())
    print('             %s^^' % (' ' * 32))

    if cur[0] == NEW:
        print('\n  ★ 已是补丁后的值，无需修改')
        return 0
    if cur[0] != OLD:
        print('\n  ✗ 既非 0x%02X 也非 0x%02X，拒绝' % (OLD, NEW))
        return 2

    if args.check:
        print('\n[dry-run] 检查通过，--apply 才写入')
        return 0

    # 备份目标块
    BACKUP.mkdir(parents=True, exist_ok=True)
    tag = datetime.now().strftime('%Y%m%d-%H%M%S')
    bk = BACKUP / ('caution-block-%s.bin' % tag)
    bk.write_bytes(fb.read_part_range(PART, blkno * fs.bs, fs.bs))
    print('\n  已备份目标块 → %s' % bk)

    # 写入：ext2 块 1024 B，sparse 要求 4096 B 对齐 ⇒ 用含目标字节的 4 KiB 窗口
    win = (part_off // fb.BLK) * fb.BLK
    region = fb.Region(PART, win, fb.BLK)
    s = part_off - region.offset
    before = region.buf[s]
    region.buf[s] = NEW
    n = region.flush()
    print('  写入 4 KiB 窗口 0x%X（原字节 0x%02X → 0x%02X），共 %d B' % (win, before, NEW, n))

    back = fb.read_part_range(PART, part_off, 1)
    print('  回读: 0x%02X  %s' % (back[0], '✓ 成功' if back[0] == NEW else '✗ 失败'))
    print('\n  提示：还需用上游 tools/wifi.py 写三个 Backup 属性')
    return 0 if back[0] == NEW else 3


if __name__ == '__main__':
    sys.exit(main())

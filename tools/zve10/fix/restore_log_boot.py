#!/usr/bin/env python3
"""恢复 /log 的引导扇区（被误写覆盖时用）。

只写 1 个 512 字节扇区，不碰数据区。
来源：本地 nflasha11.img 的引导扇区；写前校验它是有效 FAT16。
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'dev'))
import fb  # noqa: E402

PART = 'nflasha11'
LOCAL_IMG = Path('/root/fw/zve10_backup/nflasha11.img')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--source', default=str(LOCAL_IMG))
    a = ap.parse_args()

    src = Path(a.source)
    if not src.exists():
        raise SystemExit('本地镜像不存在: %s' % src)
    with open(src, 'rb') as f:
        boot = f.read(512)

    # 校验
    if boot[510:512] != b'\x55\xaa':
        raise SystemExit('源引导扇区签名不对')
    bps = struct.unpack_from('<H', boot, 11)[0]
    spc = boot[13]
    rsvd = struct.unpack_from('<H', boot, 14)[0]
    rootE = struct.unpack_from('<H', boot, 17)[0]
    fatsz = struct.unpack_from('<H', boot, 22)[0]
    fs = boot[54:62].decode('latin1', 'replace').strip('\x00 ')
    print('源引导扇区:')
    print('  bps=%d spc=%d rsvd=%d rootE=%d fatsz=%d fs=%r' % (bps, spc, rsvd, rootE, fatsz, fs))
    if fs != 'FAT16' or bps != 512 or spc != 32:
        raise SystemExit('源不是预期的 FAT16 几何')

    print()
    cur = fb.read_part_range(PART, 0, 512)
    print('相机当前引导扇区:')
    print('  签名: %s %s' % (cur[510:512].hex(),
                           '✓' if cur[510:512] == b'\x55\xaa' else '✗ 损坏'))
    print('  前 16 B: %s' % cur[:16].hex())

    if cur[510:512] == b'\x55\xaa' and cur[:3] == boot[:3]:
        print('\n  引导扇区看起来正常，无需恢复')
        return 0

    if not a.apply:
        print('\n[dry-run] 加 --apply 恢复')
        return 0

    print('\n写入引导扇区 …')
    # 512 字节 = 1 扇区，按 4 KiB 窗口写（读 4K、改前 512、写回）
    win = fb.Region(PART, 0, fb.BLK)
    win.buf[0:512] = boot
    win.flush()

    back = fb.read_part_range(PART, 0, 512)
    ok = back == boot
    print('  回读签名: %s %s' % (back[510:512].hex(), '✓' if ok else '✗'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main() or 0)

#!/usr/bin/env python3
"""替换 FAT 分区上已存在的文件（保持簇链和目录项不变，只改数据）。

比删了重建安全：目录项、短名、LFN、簇号全都不动。
前提：新文件长度不超过原文件占用簇的容量。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'dev'))

import fatio  # noqa: E402
import fb     # noqa: E402


def write_unaligned(part, part_off, blob):
    end = part_off + len(blob)
    s = (part_off // fb.BLK) * fb.BLK
    e = ((end + fb.BLK - 1) // fb.BLK) * fb.BLK
    region = fb.Region(part, s, e - s)
    o = part_off - region.offset
    region.buf[o:o + len(blob)] = blob
    region.flush()


def replace(part, name, data, apply=False):
    v = fatio.Volume(part)
    e = v.entry(name)
    if not e:
        raise SystemExit('文件不存在: %s' % name)

    fat = v.fat_bytes()
    chain = v.chain(fat, e['cluster'])
    capacity = len(chain) * v.cluster_bytes

    print('  文件    : %s' % name)
    print('  原大小  : %d B' % e['size'])
    print('  新大小  : %d B' % len(data))
    print('  簇链    : %s (%d 簇, 容量 %d B)' % (chain, len(chain), capacity))

    if len(data) > capacity:
        raise SystemExit('新文件超出簇链容量（需要 %d，有 %d）' % (len(data), capacity))

    if not apply:
        print('  [dry-run]')
        return

    data_part_off = (v.data_lba - v.base) * v.bps
    padded = data + b'\x00' * (capacity - len(data))

    for i, c in enumerate(chain):
        chunk = padded[i * v.cluster_bytes:(i + 1) * v.cluster_bytes]
        off = data_part_off + (c - 2) * v.cluster_bytes
        write_unaligned(part, off, chunk)

    # 更新目录项大小
    raw_root = bytearray(fb.read_lba(v.root_lba, (v.rootE * 32) // v.bps))
    # 找目录项
    import struct
    for off in range(0, len(raw_root), 32):
        if raw_root[off] == 0:
            break
        if raw_root[off] in (0xE5,):
            continue
        if raw_root[off + 11] == 0x0F:
            continue
        if struct.unpack_from('<H', raw_root, off + 26)[0] == e['cluster']:
            struct.pack_into('<I', raw_root, off + 28, len(data))
            break

    root_off = (v.root_lba - v.base) * v.bps
    write_unaligned(part, root_off, bytes(raw_root))

    # 验证
    v2 = fatio.Volume(part)
    got, e2 = v2.read_file(name)
    ok = got[:len(data)] == data and e2['size'] == len(data)
    print('  回读 %d B, size=%d  %s' % (len(got), e2['size'], '✓' if ok else '✗'))
    if not ok:
        raise SystemExit('回读不一致')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', default='nflasha11')
    ap.add_argument('--name', required=True)
    ap.add_argument('--file', required=True)
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    replace(a.part, a.name, Path(a.file).read_bytes(), a.apply)

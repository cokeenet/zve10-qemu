#!/usr/bin/env python3
"""向 FAT 分区写入一个新文件（含 LFN 长文件名）。

流程：
  ① 在根目录找连续空闲目录项（LFN 数 + 1）
  ② 在 FAT 里找连续空闲簇
  ③ 写数据
  ④ 建 FAT 链
  ⑤ 写目录项（短名 + LFN）
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'dev'))

import fatio  # noqa: E402
import fb     # noqa: E402

ATTR_LFN = 0x0F
ATTR_ARCHIVE = 0x20


def lfn_checksum(short_name):
    """FAT 长文件名校验和。short_name 必须是 11 字节。"""
    s = 0
    for c in short_name:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def make_lfn_entries(name, short, checksum):
    """生成 LFN 目录项（倒序，因为 FAT 从最后一段开始存）。"""
    # LFN 名用 UTF-16LE，末尾补 0x0000 再补 0xFFFF
    u = name.encode('utf-16-le')
    chars = [u[i:i + 2] for i in range(0, len(u), 2)]
    # 每个 LFN 项存 13 个字符
    chunks = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    if len(chars) % 13 == 0:
        chunks.append([])
    total = len(chunks)

    entries = []
    for idx in range(total, 0, -1):
        chunk = chunks[idx - 1]
        # 13 个 UTF-16 字符位
        slots = list(chunk)
        if len(slots) < 13:
            slots.append(b'\x00\x00')
            while len(slots) < 13:
                slots.append(b'\xff\xff')
        raw = b''.join(slots)
        seq = idx | (0x40 if idx == total else 0)
        e = bytearray(32)
        e[0] = seq
        e[1:11] = raw[0:10]      # 5 个字符
        e[11] = ATTR_LFN
        e[12] = 0
        e[13] = checksum
        e[14:26] = raw[10:22]    # 6 个字符
        e[26:28] = b'\x00\x00'
        e[28:32] = raw[22:26]    # 2 个字符
        entries.append(bytes(e))
    return entries


def make_short_name(name, existing):
    """生成 8.3 短名。冲突时加 ~N 后缀。"""
    up = name.upper()
    if '.' in up:
        base, ext = up.rsplit('.', 1)
    else:
        base, ext = up, ''
    base = ''.join(c for c in base if c.isalnum() or c in '_-')
    ext = ''.join(c for c in ext if c.isalnum() or c in '_-')[:3]

    def fit(b):
        return b[:8].ljust(8).encode('ascii')

    candidate = fit(base) + ext.ljust(3).encode('ascii')
    if candidate not in existing:
        return candidate

    for n in range(1, 100):
        suffix = '~%d' % n
        stem = base[:8 - len(suffix)] + suffix
        candidate = fit(stem) + ext.ljust(3).encode('ascii')
        if candidate not in existing:
            return candidate
    raise RuntimeError('短名冲突太多')


def write_unaligned(part, part_off, blob):
    """在分区内任意偏移写 blob（内部按 4 KiB 块读改写）。"""
    end = part_off + len(blob)
    s = (part_off // fb.BLK) * fb.BLK
    e = ((end + fb.BLK - 1) // fb.BLK) * fb.BLK
    region = fb.Region(part, s, e - s)
    o = part_off - region.offset
    region.buf[o:o + len(blob)] = blob
    region.flush()


def write_file(part, path, data, apply=False):
    """把 data 写成 path（如 /newmenu.so）的文件名部分。"""
    v = fatio.Volume(part)
    name = path.lstrip('/')

    # 现有目录项
    raw_root = bytearray(fb.read_lba(v.root_lba, (v.rootE * 32) // v.bps))
    existing_short = set()
    used_slots = set()
    free_start = None
    i = 0
    while i < len(raw_root):
        e = raw_root[i:i + 32]
        if e[0] == 0:
            if free_start is None:
                free_start = i
            # 后面都是空的
            break
        if e[0] == 0xE5:
            if free_start is None:
                free_start = i
        else:
            free_start = None
            if e[11] != ATTR_LFN:
                existing_short.add(bytes(e[0:11]))
        i += 32

    if free_start is None:
        raise RuntimeError('根目录已满')

    short = make_short_name(name, existing_short)
    csum = lfn_checksum(short)
    lfn = make_lfn_entries(name, short, csum)
    need_slots = len(lfn) + 1

    # 检查空间够不够
    avail = 0
    for j in range(free_start, len(raw_root), 32):
        e = raw_root[j:j + 32]
        if e[0] in (0x00, 0xE5):
            avail += 1
        else:
            break
    if avail < need_slots:
        raise RuntimeError('根目录空闲项不足：需要 %d，有 %d' % (need_slots, avail))

    # 找空闲簇
    ncl = (len(data) + v.cluster_bytes - 1) // v.cluster_bytes
    total = (v.fatsz * v.bps * 8) // (12 if v.bits12 else 16)
    fat = bytearray(v.fat_bytes())
    free = [c for c in range(2, total) if v.get_next(fat, c) == 0]
    if len(free) < ncl:
        raise RuntimeError('空闲簇不足：需要 %d，有 %d' % (ncl, len(free)))
    use = free[:ncl]

    print('  文件名   : %s' % name)
    print('  短名     : %s.%s' % (short[:8].decode().strip(), short[8:].decode().strip()))
    print('  LFN 项   : %d' % len(lfn))
    print('  目录项起点: 0x%X' % free_start)
    print('  需要簇   : %d  → %s' % (ncl, use))

    if not apply:
        print('  [dry-run]')
        return

    # 写数据
    # ★ 簇号 → 分区内字节偏移（必须加上 data_lba 相对分区起点的偏移）
    # ★ 该偏移通常不是 4 KiB 对齐，所以走 write_unaligned
    data_part_off = (v.data_lba - v.base) * v.bps
    for idx, c in enumerate(use):
        chunk = data[idx * v.cluster_bytes:(idx + 1) * v.cluster_bytes]
        if len(chunk) < v.cluster_bytes:
            chunk += b'\x00' * (v.cluster_bytes - len(chunk))
        off = data_part_off + (c - 2) * v.cluster_bytes
        write_unaligned(part, off, chunk)

    # 建 FAT 链
    for idx, c in enumerate(use):
        nxt = use[idx + 1] if idx + 1 < len(use) else v.eoc()
        v.set_next(fat, c, nxt)

    # 写目录项
    off = free_start
    for e in lfn:
        raw_root[off:off + 32] = e
        off += 32
    entry = bytearray(32)
    entry[0:11] = short
    entry[11] = ATTR_ARCHIVE
    struct.pack_into('<H', entry, 26, use[0])
    struct.pack_into('<I', entry, 28, len(data))
    raw_root[off:off + 32] = entry

    # 回写：先数据簇（已写），再 FAT，最后目录项
    # 注意顺序：先 FAT 再目录项，避免目录项指向还没标记的簇
    fat_off = (v.fat_lba - v.base) * v.bps
    root_off = (v.root_lba - v.base) * v.bps

    write_unaligned(part, fat_off, bytes(fat))
    write_unaligned(part, root_off, bytes(raw_root))
    print('  已写入')

    # ★ 回读验证：文件内容、引导扇区、FAT、目录项都要对
    print()
    print('  回读验证 …')
    v2 = fatio.Volume(part)          # 顺带验证引导扇区还能解析
    e2 = v2.entry(name)
    if not e2:
        raise RuntimeError('回读失败：目录项不存在')
    got, _ = v2.read_file(name)
    if got != data:
        raise RuntimeError('回读失败：内容不一致（%d vs %d B）' % (len(got), len(data)))
    print('    内容 %d B 一致 ✓' % len(got))
    print('    引导扇区可解析 ✓')
    print('    cluster=%d size=%d' % (e2['cluster'], e2['size']))


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', default='nflasha11')
    ap.add_argument('--name', required=True)
    ap.add_argument('--file', required=True)
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    data = Path(a.file).read_bytes()
    print('源: %s  %d B' % (a.file, len(data)))
    write_file(a.part, a.name, data, a.apply)

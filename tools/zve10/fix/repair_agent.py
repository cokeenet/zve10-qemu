#!/usr/bin/env python3
"""修复 /log/agent_v14.so 的 FAT 簇链并重写数据。

症状：文件声明 18,104 B，但簇链只有 2 簇（16,384 B），尾部 1,720 B 是垃圾。

做法：
  ① 备份当前 FAT + 目录项
  ② 找足够大的空闲簇
  ③ 写正确数据
  ④ 重建簇链
  ⑤ 更新目录项 cluster/size
  ⑥ 回读校验 md5

用法:
    python3 tools/zve10/fix/repair_agent.py            # dry-run
    python3 tools/zve10/fix/repair_agent.py --apply
"""
import argparse
import hashlib
import json
import struct
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'fat'))
sys.path.insert(0, str(HERE.parent / 'dev'))

import fb       # noqa: E402
import fatio    # noqa: E402

BACKUP = HERE.parent / 'backups'
SOURCE = HERE.parents[2] / 'tools' / 'payload' / 'vendor' / 'agent_v14.so'


def _write_unaligned(v, part_off, data):
    """写到分区内任意偏移：读覆盖的 4 KiB 块，改，整块写回。"""
    end = part_off + len(data)
    start_blk = (part_off // fb.BLK) * fb.BLK
    end_blk = ((end + fb.BLK - 1) // fb.BLK) * fb.BLK
    span = end_blk - start_blk
    region = fb.Region(v.part, start_blk, span)
    s = part_off - region.offset
    region.buf[s:s + len(data)] = data
    region.flush()


def find_entry_slot(v):
    """返回 agent_v14.so 在根目录里的 32B 项偏移。"""
    raw = fb.read_lba(v.root_lba, (v.rootE * 32) // v.bps)
    off = 0
    while off < len(raw):
        e = raw[off:off + 32]
        if e[0] == 0:
            break
        if e[0] != 0xE5 and e[11] != fatio.LONG:
            short = e[:8].decode('latin1').rstrip()
            ext = e[8:11].decode('latin1').rstrip()
            full = (short + ('.' + ext if ext else '')).lower()
            # 同时接受短名（AGENT_~1.SO）和长名
            if full in ('agent_v14.so', 'agent_~1.so'):
                return off, bytes(raw)
        off += 32
    raise SystemExit('根目录里找不到 agent_v14.so')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--source', default=str(SOURCE))
    args = ap.parse_args()

    src = Path(args.source).read_bytes()
    print('源文件: %s  %d B  md5=%s' % (args.source, len(src), hashlib.md5(src).hexdigest()))

    v = fatio.Volume('nflasha11')
    ent_off, root_raw = find_entry_slot(v)
    fat = bytearray(v.fat_bytes())

    cur_cluster = struct.unpack_from('<H', root_raw, ent_off + 26)[0]
    cur_size = struct.unpack_from('<I', root_raw, ent_off + 28)[0]
    print('当前目录项: cluster=%d size=%d' % (cur_cluster, cur_size))

    chain = v.chain(fat, cur_cluster)
    print('当前簇链: %s  (%d B 容量)' % (chain, len(chain) * v.cluster_bytes))

    need = (len(src) + v.cluster_bytes - 1) // v.cluster_bytes
    print('需要簇数: %d  (每簇 %d B)' % (need, v.cluster_bytes))

    # 找空闲簇
    total = (v.fatsz * v.bps * 8) // (12 if v.bits12 else 16)
    free = [c for c in range(2, total) if v.get_next(fat, c) == 0]
    print('空闲簇数量: %d' % len(free))
    if len(free) < need:
        raise SystemExit('空闲簇不足')

    use = free[:need]
    print('将使用簇: %s' % use)

    if not args.apply:
        print('\n[dry-run] 加 --apply 执行')
        return

    # 备份
    BACKUP.mkdir(parents=True, exist_ok=True)
    tag = datetime.now().strftime('%Y%m%d-%H%M%S')
    (BACKUP / ('repair-%s-fat.bin' % tag)).write_bytes(bytes(fat))
    (BACKUP / ('repair-%s-root.bin' % tag)).write_bytes(root_raw)
    (BACKUP / ('repair-%s.json' % tag)).write_text(json.dumps(dict(
        old_cluster=cur_cluster, old_size=cur_size, old_chain=chain,
        new_clusters=use, new_size=len(src), source=args.source,
    ), indent=1))
    print('已备份 → %s' % BACKUP)

    # ① 写数据
    print('\n① 写数据')
    data_part_off = (v.data_lba - v.base) * v.bps   # ★ 加 data 区起点偏移
    for i, c in enumerate(use):
        chunk = src[i * v.cluster_bytes:(i + 1) * v.cluster_bytes]
        if len(chunk) < v.cluster_bytes:
            chunk += b'\x00' * (v.cluster_bytes - len(chunk))
        off = data_part_off + (c - 2) * v.cluster_bytes
        fb.write_part_range('nflasha11', off, chunk)
        print('   簇 %-6d → 0x%08X  %d B' % (c, off, min(len(src) - i * v.cluster_bytes, v.cluster_bytes)))

    # ② 重建 FAT 链
    print('\n② 重建 FAT 链')
    for i, c in enumerate(use):
        nxt = use[i + 1] if i + 1 < len(use) else v.eoc()
        v.set_next(fat, c, nxt)
        print('   FAT[%d] = 0x%X' % (c, nxt))
    # 释放旧簇
    for c in chain:
        if c not in use:
            v.set_next(fat, c, 0)
            print('   释放旧簇 %d' % c)

    # ③ 更新目录项
    print('\n③ 更新目录项')
    root = bytearray(root_raw)
    struct.pack_into('<H', root, ent_off + 26, use[0])
    struct.pack_into('<I', root, ent_off + 28, len(src))
    print('   cluster=%d size=%d' % (use[0], len(src)))

    # ④ 写回（4 KiB 块级读改写）
    print('\n④ 写回 FAT + 目录项')
    _write_unaligned(v, (v.fat_lba - v.base) * v.bps, bytes(fat))
    _write_unaligned(v, (v.root_lba - v.base) * v.bps, bytes(root))
    print('   FAT + root 已写入')

    # ⑤ 回读校验
    print('\n⑤ 回读校验')
    v2 = fatio.Volume('nflasha11')
    fat2 = v2.fat_bytes()
    e2 = v2.entry('agent_v14.so')
    if e2 is None:
        raise SystemExit('回读失败：文件不见了')
    data = v2.read_clusters(v2.chain(fat2, e2['cluster']))[:e2['size']]
    ok = data == src
    print('   大小 %d (期望 %d)' % (len(data), len(src)))
    print('   md5  %s' % hashlib.md5(data).hexdigest())
    print('   %s' % ('✓ 完全一致' if ok else '✗ 不一致'))
    if not ok:
        print('   前 32 B: %s' % data[:32].hex())
        print('   期望   : %s' % src[:32].hex())
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main() or 0)

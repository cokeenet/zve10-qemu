#!/usr/bin/env python3
"""删除相机内部存储上我们自己的临时文件。

安全设计：
  · 默认 dry-run，必须显式 --apply 才写
  · 只删 inventory 判定为 "ours" 的项，原厂文件一律拒绝
  · 删除前把被删的目录项 + FAT 表备份到本地
  · 长文件名（LFN）连同它的 0x0F 目录项一起标记删除
  · 每删一个都回读验证

用法:
    # 只看会删什么
    python3 tools/zve10/cleanup/delete_files.py

    # 真的删
    python3 tools/zve10/cleanup/delete_files.py --apply

    # 只删某个分区
    python3 tools/zve10/cleanup/delete_files.py --part nflasha11

    # 白名单：保留某些文件不删
    python3 tools/zve10/cleanup/delete_files.py --keep agent_v14.so --keep zve10_agent.log
"""
import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'fat'))
sys.path.insert(0, str(HERE.parent / 'dev'))
sys.path.insert(0, str(HERE.parent / 'inspect'))

import fb          # noqa: E402
import fatio       # noqa: E402
import inventory   # noqa: E402

BACKUP_DIR = HERE.parent / 'backups'


def _write_unaligned(v, part_off, data):
    """把 data 写到分区内任意偏移：先读覆盖到的 4 KiB 块，改中间，整块写回。"""
    end = part_off + len(data)
    start_blk = (part_off // fb.BLK) * fb.BLK
    end_blk = ((end + fb.BLK - 1) // fb.BLK) * fb.BLK
    span = end_blk - start_blk
    region = fb.Region(v.part, start_blk, span)
    s = part_off - region.offset
    region.buf[s:s + len(data)] = data
    region.flush()
    return len(data)


def _pad4k(data):
    """补齐到 4 KiB 整数倍（sparse 写入要求）。"""
    r = len(data) % fb.BLK
    return data if r == 0 else data + b'\x00' * (fb.BLK - r)


def plan(part, keep):
    """算出要删的目录项（含 LFN 前缀）。"""
    v = fatio.Volume(part)
    raw_root = fb.read_lba(v.root_lba, (v.rootE * 32) // v.bps)

    # 先解析出每个文件占哪些 32B 项
    items = []
    lfn_stack = []
    for i in range(0, len(raw_root), 32):
        e = raw_root[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == fatio.DELETED:
            lfn_stack.clear()
            continue
        if e[11] == fatio.LONG:
            lfn_stack.append(i)
            continue
        if e[0] == ord('.'):
            lfn_stack.clear()
            continue
        short = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        # 长名
        long_name = ''
        if lfn_stack:
            parts = {}
            for off in lfn_stack:
                le = raw_root[off:off + 32]
                body = le[1:11] + le[14:26] + le[28:32]
                s = ''
                for j in range(0, len(body), 2):
                    c = body[j] | (body[j + 1] << 8)
                    if c in (0, 0xFFFF):
                        break
                    s += chr(c)
                parts[le[0] & 0x3F] = s
            if parts:
                long_name = ''.join(parts.get(k, '?') for k in range(1, max(parts) + 1))
        name = long_name or (short + ('.' + ext if ext else ''))
        items.append(dict(
            name=name, short=short, ext=ext, ent_off=i,
            lfn_offs=list(lfn_stack),
            cluster=int.from_bytes(e[26:28], 'little'),
            size=int.from_bytes(e[28:32], 'little'),
            attr=e[11],
        ))
        lfn_stack = []

    # 分类 + 过滤
    doomed, skipped = [], []
    for it in items:
        kind = inventory.classify(part, it['name'], it['attr'], it['size'])
        if kind != 'ours':
            skipped.append((it['name'], '归属=%s，不删' % kind))
        elif it['name'] in keep:
            skipped.append((it['name'], '在 --keep 白名单'))
        else:
            doomed.append(it)
    return v, raw_root, doomed, skipped


def dir_children(v, cluster):
    """列出某个目录簇里的所有子项（含 LFN 偏移）。"""
    fat = v.fat_bytes()
    raw = v.read_clusters(v.chain(fat, cluster))
    out, lfn = [], []
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == fatio.DELETED:
            lfn.clear(); continue
        if e[11] == fatio.LONG:
            lfn.append(i); continue
        if e[0] == ord('.'):
            lfn.clear(); continue
        out.append(dict(ent_off=i, lfn_offs=list(lfn),
                        cluster=int.from_bytes(e[26:28], 'little'),
                        size=int.from_bytes(e[28:32], 'little'),
                        attr=e[11]))
        lfn.clear()
    return raw, out


def wipe_dir(v, cluster):
    """递归清空目录：先标记子项删除，再把目录自身的数据簇清零。"""
    fat = bytearray(v.fat_bytes())
    raw, kids = dir_children(v, cluster)
    buf = bytearray(raw)
    for k in kids:
        if k['attr'] & fatio.DIRENT:
            wipe_dir(v, k['cluster'])
        for off in k['lfn_offs']:
            buf[off] = fatio.DELETED
        buf[k['ent_off']] = fatio.DELETED
        # 释放子项簇链
        c = k['cluster']; guard = 0
        while 2 <= c < (0xFF8 if v.bits12 else 0xFFF8) and guard < 65536:
            nxt = v.get_next(fat, c)
            v.set_next(fat, c, 0)
            c = nxt; guard += 1
    # 写回目录内容（按 4 KiB 对齐的簇写）
    chain = v.chain(fat, cluster)
    for i, c in enumerate(chain):
        chunk = bytes(buf[i * v.cluster_bytes:(i + 1) * v.cluster_bytes])
        if len(chunk) < v.cluster_bytes:
            chunk += b'\x00' * (v.cluster_bytes - len(chunk))
        off = ((c - 2) * v.spc) * v.bps
        fb.write_part_range(v.part, off, chunk)
    return bytes(fat)


def backup_and_delete(part, v, raw_root, doomed, tag):
    """备份 + 执行删除 + 验证。"""
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    fat = bytearray(v.fat_bytes())
    root = bytearray(raw_root)

    # 备份
    (BACKUP_DIR / ('%s-%s-fat.bin' % (part, tag))).write_bytes(bytes(fat))
    (BACKUP_DIR / ('%s-%s-root.bin' % (part, tag))).write_bytes(bytes(root))
    meta = dict(part=part, fstype=v.fstype, base=hex(v.base),
                fat_lba=hex(v.fat_lba), root_lba=hex(v.root_lba),
                deleted=[dict(name=d['name'], ent_off=d['ent_off'],
                              lfn_offs=d['lfn_offs'], cluster=d['cluster'],
                              size=d['size']) for d in doomed])
    (BACKUP_DIR / ('%s-%s.json' % (part, tag))).write_text(
        json.dumps(meta, ensure_ascii=False, indent=1))

    # 标记目录项为已删除
    for d in doomed:
        if d['attr'] & fatio.DIRENT:
            newfat = wipe_dir(v, d['cluster'])
            fat = bytearray(newfat)
        for off in d['lfn_offs']:
            root[off] = fatio.DELETED
        root[d['ent_off']] = fatio.DELETED
        # 释放簇链
        c = d['cluster']
        guard = 0
        while 2 <= c < (0xFF8 if v.bits12 else 0xFFF8) and guard < 65536:
            nxt = v.get_next(fat, c)
            v.set_next(fat, c, 0)
            c = nxt
            guard += 1

    # 写回：FAT 和 root 不在 4 KiB 边界上，所以按「读整块→改→写整块」做
    _write_unaligned(v, (v.fat_lba - v.base) * v.bps, bytes(fat))
    _write_unaligned(v, (v.root_lba - v.base) * v.bps, bytes(root))

    # 验证
    ver = fatio.Volume(part)
    left = {e['name'] for e in ver.listdir()}
    failed = [d['name'] for d in doomed if d['name'] in left]
    return failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true', help='真的写（默认 dry-run）')
    ap.add_argument('--part', default='all', choices=['all', 'nflasha2', 'nflasha11'])
    ap.add_argument('--keep', action='append', default=[], help='保留的文件名')
    ap.add_argument('--tag', default=None, help='备份文件名后缀')
    args = ap.parse_args()

    tag = args.tag or datetime.now().strftime('%Y%m%d-%H%M%S')
    parts = ['nflasha2', 'nflasha11'] if args.part == 'all' else [args.part]

    grand = 0
    for part in parts:
        v, raw_root, doomed, skipped = plan(part, set(args.keep))
        label = {'nflasha2': '/setting', 'nflasha11': '/log'}[part]

        print('\n=== %s (%s) ===' % (label, v.fstype))
        if not doomed:
            print('  没有可删除的项')
        else:
            print('  将删除 %d 项，%d B：' % (len(doomed), sum(d['size'] for d in doomed)))
            for d in sorted(doomed, key=lambda x: -x['size']):
                lfn = ' (+%d LFN)' % len(d['lfn_offs']) if d['lfn_offs'] else ''
                print('    %-40s %10d B%s' % (d['name'], d['size'], lfn))
            grand += sum(d['size'] for d in doomed)

        if skipped:
            print('  保留 %d 项：' % len(skipped))
            for name, why in sorted(skipped)[:12]:
                print('    %-40s %s' % (name, why))
            if len(skipped) > 12:
                print('    … 另有 %d 项' % (len(skipped) - 12))

        if args.apply and doomed:
            print('  备份到 %s …' % BACKUP_DIR)
            failed = backup_and_delete(part, v, raw_root, doomed, tag)
            if failed:
                print('  ✗ %d 项删除后仍在：%s' % (len(failed), failed))
            else:
                print('  ✓ 全部删除并已验证')

    print('\n=== 合计 ===')
    print('  %d B (%.3f MB)' % (grand, grand / 1048576))
    if not args.apply:
        print('\n  这是 dry-run。加 --apply 才会真的写入。')


if __name__ == '__main__':
    main()

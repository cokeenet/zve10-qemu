#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""backup_db v3 —— 在 v2(5 条真实记录)基础上,把 App 实际请求过的全部 id
补成 4 字节零记录(自洽合成数据,非随机填充)。

判决性实验:若"DB 空库"是根因,补零后 App 的读应从 -1 变为 4 字节 0,
TextRM 断言应消失或换点。若 reply 仍 -1,则回复管道有 bug。

用法:
    make_backup_db_v3.py [OUT] [--ids-file FILE] [--gdb-log FILE]

    OUT            输出文件，默认 backup_db_v3.bin（当前目录）
    --ids-file     十六进制 id 清单，每行一个，# 开头为注释
                   默认用与本脚本同目录的 requested_ids.txt
    --gdb-log      备选：直接从 GDB 抓包日志里正则提取
                   （匹配 '@@@SEND qid=.. opcode=.. id=XXXXXXXX'）

示例:
    python3 make_backup_db_v3.py
    python3 make_backup_db_v3.py my_db.bin --ids-file my_ids.txt
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import make_backup_db as base

HERE = Path(__file__).resolve().parent
DEFAULT_IDS = HERE / 'requested_ids.txt'

# 页号/子号上限:超出视为池残影（payload 未写入的槽会产生垃圾 id）
MAX_PAGE = 0x332
MAX_SUB = 0x2000


def load_ids_from_txt(path):
    """从纯 id 清单读取（每行一个 hex，# 注释）"""
    ids = set()
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        ids.add(int(line, 16))
    return ids


def load_ids_from_gdb_log(path):
    """从 GDB 抓包日志提取"""
    data = Path(path).read_bytes().decode('latin1')
    return {int(x, 16) for x in
            re.findall(r'@@@SEND qid=\w+ opcode=\d+ id=(\w+)', data)}


def main() -> int:
    ap = argparse.ArgumentParser(
        description='生成含零值记录的 backup_db（供 QEMU 装载器实验用）')
    ap.add_argument('out', nargs='?', default='backup_db_v3.bin',
                    help='输出文件（默认 backup_db_v3.bin）')
    ap.add_argument('--ids-file', default=None,
                    help='十六进制 id 清单（默认同目录 requested_ids.txt）')
    ap.add_argument('--gdb-log', default=None,
                    help='从 GDB 日志提取 id（与 --ids-file 二选一）')
    args = ap.parse_args()

    if args.gdb_log:
        ids = load_ids_from_gdb_log(args.gdb_log)
        src = 'GDB 日志 %s' % args.gdb_log
    else:
        f = Path(args.ids_file) if args.ids_file else DEFAULT_IDS
        if not f.exists():
            print('!! 找不到 id 清单: %s' % f, file=sys.stderr)
            print('   请用 --ids-file 指定，或 --gdb-log 从日志提取', file=sys.stderr)
            return 2
        ids = load_ids_from_txt(f)
        src = 'id 清单 %s' % f

    if not ids:
        print('!! %s 里没解析出任何 id' % src, file=sys.stderr)
        return 2

    # 过滤池残影
    good = sorted(k for k in ids if (k >> 16) <= MAX_PAGE and (k & 0xFFFF) <= MAX_SUB)
    print('来源: %s' % src)
    print('原始 %d 个, 过滤后 %d 个 (丢弃 %d 个池残影)'
          % (len(ids), len(good), len(ids) - len(good)))

    base_ids = {0x01120001: base.REC_BIN, 0x01700000: base.REC_SET,
                0x0051000E: base.REC_ULOG,
                0x00FA0028: struct.pack('<I', 0x8F000000),
                0x00FA005D: struct.pack('<I', 0x00080000)}
    zero4 = b'\x00\x00\x00\x00'
    # 0x00810000 = TextRM 字体列表路径（libObj 0x8fc2c 会 fopen 该路径内容）
    recs_extra = {0x00810000: b'/usr/share/app/fontlist.dat\x00'}
    recs = dict(base_ids)
    for k in good:
        if k not in recs:
            recs[k] = zero4
    recs.update(recs_extra)
    print('总记录 %d 条' % len(recs))

    # ---- 布局：头 0x100 + 页表(6B/页) + 槽表(6B/槽) + 数据 ----
    HDR = 0x100
    pages = {}
    for k in recs:
        pages.setdefault(k >> 16, []).append(k & 0xFFFF)
    page_list = sorted(pages)
    N1 = max(page_list) + 1

    slots = []
    page_meta = {}
    for p in page_list:
        cnt = max(pages[p]) + 1
        page_meta[p] = (cnt, len(slots))
        slots.extend([0xFFFFFFFF] * cnt)
    for k in recs:
        p, s = k >> 16, k & 0xFFFF
        cnt, cum = page_meta[p]
        slots[cum + s] = None

    slot_off = HDR + 6 * N1
    data_off = slot_off + 6 * len(slots)
    buf = bytearray(data_off)
    cur = data_off
    for k in sorted(recs):
        c = recs[k]
        p, s = k >> 16, k & 0xFFFF
        cnt, cum = page_meta[p]
        slots[cum + s] = (len(c) << 24) | cur
        buf[cur:cur + len(c)] = c
        cur += len(c)
    total = cur

    # ---- 头 ----
    struct.pack_into('<I', buf, 0x00, base.BASE_PHYS)
    struct.pack_into('>I', buf, 0x04, 0xAAAAAAAA)
    struct.pack_into('>I', buf, 0x08, 1)
    buf[0x0C:0x10] = b'BK4\x00'
    struct.pack_into('<I', buf, 0x10, N1)
    struct.pack_into('>I', buf, 0x28, 1)
    struct.pack_into('<I', buf, 0x2C, len(slots))
    for p, (cnt, cum) in page_meta.items():
        struct.pack_into('<HI', buf, HDR + 6 * p, cnt, cum)
    for so_i, v in enumerate(slots):
        if v is not None:
            struct.pack_into('<I', buf, slot_off + 6 * so_i + 2, v)
    struct.pack_into('<I', buf, 0x20, 0)
    struct.pack_into('<I', buf, 0x20, sum(buf) & 0xFFFFFFFF)

    out = Path(args.out)
    out.write_bytes(bytes(buf))
    print('wrote %s  %d bytes (0x%x)  N1=%#x  slots=%d  data=%#x..%#x'
          % (out, len(buf), len(buf), N1, len(slots), data_off, total))

    # ---- 自检：用 Data_SearchId 算法回查 ----
    d = bytes(buf)

    def search(key):
        page, sub = key >> 16, key & 0xFFFF
        if page >= N1:
            return None
        cnt, cum = struct.unpack_from('<HI', d, HDR + 6 * page)
        if sub >= cnt:
            return None
        so = slot_off + 6 * (cum + sub)
        v = struct.unpack_from('<I', d, so + 2)[0]
        if v == 0xFFFFFFFF:
            return None
        typ = v >> 24
        off = v & 0xFFFFFF
        return typ, d[off:off + typ]

    ok = bad = 0
    for k in sorted(recs):
        r = search(k)
        if r and r[0] == len(recs[k]):
            ok += 1
        else:
            bad += 1
            print('  !! 自检失败 %#010x -> %r' % (k, r))
    print('自检: %d OK / %d FAIL' % (ok, bad))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

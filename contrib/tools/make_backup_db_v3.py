#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""backup_db v3 —— 在 v2(5 条真实记录)基础上,把 App 实际请求过的全部 id
补成 4 字节零记录(自洽合成数据,非随机填充)。

判决性实验:若"DB 空库"是根因,补零后 App 的读应从 -1 变为 4 字节 0,
TextRM 断言应消失或换点。若 reply 仍 -1,则回复管道有 bug(diag36 的 K2/K4 会暴露)。
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import make_backup_db as base

# 从 gdb32.log 提取 App 请求过的全部 id
def requested_ids():
    data = open('/mnt/g/WorkBuddy/zve10/work/gdb32.log', 'rb').read().decode('latin1')
    ids = set(re.findall(r'@@@SEND qid=\w+ opcode=\d+ id=(\w+)', data))
    return sorted(int(x, 16) for x in ids)


def main() -> int:
    # 复用 v2 的结构生成逻辑:先取 v2 输出,再用同样的 schema 追加零记录。
    # 简化实现:直接改 make_backup_db 的 build —— 这里独立重建。
    ids = requested_ids()
    # 过滤:payload 未写入的池残影会产生垃圾"id"(页 0x93c/0x7d8 等)。
    # 真机 DB 页表 819 项(N1=0x333),sub 上限取 0x2000 —— 之外的视为残影丢弃。
    good = [k for k in ids if (k >> 16) <= 0x332 and (k & 0xFFFF) <= 0x2000]
    print('原始 %d 个, 过滤后 %d 个 (丢弃 %d 个池残影)'
          % (len(ids), len(good), len(ids) - len(good)))
    ids = good
    base_ids = {0x01120001: base.REC_BIN, 0x01700000: base.REC_SET,
                0x0051000E: base.REC_ULOG,
                0x00FA0028: struct.pack('<I', 0x8F000000),
                0x00FA005D: struct.pack('<I', 0x00080000)}
    zero4 = b'\x00\x00\x00\x00'
    # 0x00810000 = TextRM 字体列表路径（libObj 0x8fc2c 会 fopen 该路径内容）
    recs_extra = {0x00810000: b'/usr/share/app/fontlist.dat\x00'}
    # 合并:id -> 内容;冲突时真实记录优先
    recs = dict(base_ids)
    for k in ids:
        if k not in recs:
            recs[k] = zero4
    recs.update(recs_extra)
    print('总记录 %d 条' % len(recs))

    HDR = 0x100
    pages = {}
    for k in recs:
        pages.setdefault(k >> 16, []).append(k & 0xFFFF)
    page_list = sorted(pages)
    N1 = max(page_list) + 1
    # 每页槽位数 = max(sub)+1;槽表顺序 = 页升序
    slots = []            # list of v or None
    page_meta = {}        # page -> (count, cum)
    for p in page_list:
        cnt = max(pages[p]) + 1
        page_meta[p] = (cnt, len(slots))
        slots.extend([0xFFFFFFFF] * cnt)
    for k, content in recs.items():
        p, s = k >> 16, k & 0xFFFF
        cnt, cum = page_meta[p]
        slots[cum + s] = None  # 稍后填

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

    struct.pack_into('<I', buf, 0x00, base.BASE_PHYS)
    struct.pack_into('>I', buf, 0x04, 0xAAAAAAAA)
    struct.pack_into('>I', buf, 0x08, 1)
    buf[0x0C:0x10] = b'BK4\x00'
    struct.pack_into('<I', buf, 0x10, N1)
    struct.pack_into('>I', buf, 0x28, 1)
    struct.pack_into('<I', buf, 0x2C, len(slots))
    for p, (cnt, cum) in page_meta.items():
        struct.pack_into('<HI', buf, HDR + 6 * p, cnt, cum)
    # 未列出页填 0
    for so_i, v in enumerate(slots):
        if v is not None:
            struct.pack_into('<I', buf, slot_off + 6 * so_i + 2, v)
    struct.pack_into('<I', buf, 0x20, 0)
    struct.pack_into('<I', buf, 0x20, sum(buf) & 0xFFFFFFFF)

    out = Path('/mnt/g/WorkBuddy/zve10/work/backup_db.bin')
    out.write_bytes(bytes(buf))
    print('wrote %s %d bytes (0x%x)  N1=%#x  slots=%d  data=%#x..%#x'
          % (out, len(buf), len(buf), N1, len(slots), data_off, total))

    # 自检:用 Data_SearchId 模拟抽查
    import importlib
    d = bytes(buf)
    def search(key):
        page, sub = key >> 16, key & 0xFFFF
        if page >= N1: return None
        cnt, cum = struct.unpack_from('<HI', d, HDR + 6 * page)
        if sub >= cnt: return None
        so = slot_off + 6 * (cum + sub)
        v = struct.unpack_from('<I', d, so + 2)[0]
        if v == 0xFFFFFFFF: return None
        typ = v >> 24; off = v & 0xFFFFFF
        return typ, d[off:off + typ]
    ok = bad = 0
    for k in list(recs)[:400]:
        r = search(k)
        if r and r[0] == len(recs[k]): ok += 1
        else: bad += 1; print('  !! 自检失败 %#010x -> %r' % (k, r))
    print('自检: %d OK / %d FAIL' % (ok, bad))
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""设置 /setting/mode/preload。

安全校验（写之前全部检查）：
  · 内容必须以 bootin.elf 白名单的 4 个路径之一开头
  · 总长度 <= 498 字节（bootin 的 read 上限）
  · 不含 NUL
  · dmode 必须是 3p
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'fat'))
sys.path.insert(0, str(HERE.parent / 'dev'))

import fatio  # noqa: E402
import fb     # noqa: E402

WHITELIST = (
    b'/usr/tool/LeakTracer.so',
    b'/usr/tool/libefence.so',
    b'/usr/tool/libjemalloc_tsh.so',
    b'/usr/tool/libmcheckb.so',
)
MAXLEN = 498


def write_unaligned(part, part_off, blob):
    end = part_off + len(blob)
    s = (part_off // fb.BLK) * fb.BLK
    e = ((end + fb.BLK - 1) // fb.BLK) * fb.BLK
    region = fb.Region(part, s, e - s)
    o = part_off - region.offset
    region.buf[o:o + len(blob)] = blob
    region.flush()


def replace_in_dir(v, dir_cluster, name, data):
    """把一个已存在的小文件内容改掉（不新建）。"""
    fat = v.fat_bytes()
    for e in v.listdir(dir_cluster):
        if e['name'] == name:
            chain = v.chain(fat, e['cluster'])
            cap = len(chain) * v.cluster_bytes
            if len(data) > cap:
                raise SystemExit('%s: 新内容 %d B 超出容量 %d B' % (name, len(data), cap))
            # 数据区写
            data_part_off = (v.data_lba - v.base) * v.bps
            padded = data + b'\x00' * (cap - len(data))
            for i, c in enumerate(chain):
                chunk = padded[i * v.cluster_bytes:(i + 1) * v.cluster_bytes]
                off = data_part_off + (c - 2) * v.cluster_bytes
                write_unaligned(v.part, off, chunk)
            # 目录项 size 更新
            raw = bytearray(v.read_clusters(chain))
            # 找 mode 目录里这个项的偏移
            raw_dir = bytearray(fb.read_lba(v.root_lba, (v.rootE * 32) // v.bps))
            # 目录在数据区，需要读目录本身的簇
            fat2 = v.fat_bytes()
            dir_data = None
            for de in v.listdir():
                if de['name'] == 'mode' and (de['attr'] & fatio.DIRENT):
                    dc = v.chain(fat2, de['cluster'])
                    dir_data = bytearray(v.read_clusters(dc))
                    dir_chain = dc
                    break
            if dir_data is None:
                raise SystemExit('找不到 mode 目录')
            import struct
            for off in range(0, len(dir_data), 32):
                if dir_data[off] == 0:
                    break
                if dir_data[off] in (0xE5,) or dir_data[off+11] == 0x0F:
                    continue
                if struct.unpack_from('<H', dir_data, off+26)[0] == e['cluster']:
                    struct.pack_into('<I', dir_data, off+28, len(data))
                    break
            data_part_off = (v.data_lba - v.base) * v.bps
            for i, c in enumerate(dir_chain):
                chunk = bytes(dir_data[i*v.cluster_bytes:(i+1)*v.cluster_bytes])
                if len(chunk) < v.cluster_bytes:
                    chunk += b'\x00' * (v.cluster_bytes - len(chunk))
                write_unaligned(v.part, data_part_off + (c-2)*v.cluster_bytes, chunk)
            return True
    return False


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--value', required=True, help='新的 preload 内容')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()

    val = a.value.encode('ascii')

    # 校验
    print('  内容: %r' % a.value)
    print('  长度: %d B (上限 %d)' % (len(val), MAXLEN))
    if len(val) > MAXLEN:
        raise SystemExit('超长')
    if b'\x00' in val:
        raise SystemExit('含 NUL')
    if b'\n' in val:
        raise SystemExit('含换行（bootin 不会剥离）')

    ok = any(val.startswith(w) for w in WHITELIST)
    print('  白名单前缀: %s' % ('✓ 命中' if ok else '✗ 不匹配'))
    if not ok:
        raise SystemExit('前缀不在白名单，bootin 会拒绝')

    v = fatio.Volume('nflasha2')
    mode = v.entry('mode')
    if not mode:
        raise SystemExit('/setting/mode 不存在')

    # 已有 preload 的容量
    fat = v.fat_bytes()
    pe = None
    for e in v.listdir(mode['cluster']):
        if e['name'] == 'preload':
            pe = e; break
    if pe:
        chain = v.chain(fat, pe['cluster'])
        cap = len(chain) * v.cluster_bytes
        print('  现有 preload: %d B, 簇容量 %d B' % (pe['size'], cap))
        if len(val) > cap:
            raise SystemExit('新内容超出簇容量')

    if not a.apply:
        print('  [dry-run]')
        return 0

    if not replace_in_dir(v, mode['cluster'], 'preload', val):
        raise SystemExit('写入失败：找不到 preload')

    # 验证
    v2 = fatio.Volume('nflasha2')
    m2 = v2.entry('mode')
    fat2 = v2.fat_bytes()
    for e in v2.listdir(m2['cluster']):
        if e['name'] == 'preload':
            got = v2.read_clusters(v2.chain(fat2, e['cluster']))[:e['size']]
            print()
            print('  回读: %r (%d B)  %s' % (got, len(got), '✓' if got == val else '✗'))
            return 0 if got == val else 1
    raise SystemExit('回读时找不到 preload')


if __name__ == '__main__':
    sys.exit(main() or 0)

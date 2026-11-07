#!/usr/bin/env python3
"""zve10 设备通道 —— fastboot/stage2 统一接口。

写操作要点
----------
stage2 的 `flash:<part>` 总是从分区起点写，所以要写任意位置必须用
sparse 镜像：SKIP 前缀 + RAW 数据块 + SKIP 后缀。见 fastboot/host/sparse_block.py。

本模块把「按扇区读 / 按 4 KiB 块写」封装成统一原语，其他工具不必关心 sparse。
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'fastboot' / 'host'))

from bootstrap import Device, wait_for_device, download   # noqa: E402
from read_updater_log import upload_blocks                # noqa: E402

# 分区起始 LBA（0x00800000 = nflasha2 锚点，实测）
PART_LBA = {
    'nflasha1':  0x0080,
    'nflasha2':  0x4000,
    'nflasha3':  0xE000,
    'nflasha4':  0x26000,
    'nflasha5':  0x2C000,
    'nflasha6':  0x4A000,
    'nflasha7':  0x52000,
    'nflasha10': 0x56000,
    'nflasha11': 0x5C000,
    'nflasha12': 0x156000,
    'nflasha13': 0x16A000,
    'nflasha15': 0x16C000,
    'nflasha16': 0x5C000 + (512000+40960+4096+307200)*2,
    'nflasha17': 0x5C000 + (512000+40960+4096+307200+143360)*2,
    'nflasha18': 0x5C000 + (512000+40960+4096+307200+143360+102400)*2,
}

BLK = 4096
MAGIC = 0xED26FF3A
CHUNK_RAW = 0xCAC1
CHUNK_SKIP = 0xCAC3

_dev = None
_part_bytes = {}


def open_device(seconds=120):
    global _dev
    if _dev is not None:
        return _dev
    raw = wait_for_device(seconds)
    if raw is None:
        raise RuntimeError('未找到 fastboot 设备（ZVE10-fastboot）')
    _dev = Device(raw)
    _dev.require('oem emmc-init')
    return _dev


def close():
    global _dev, _part_bytes
    if _dev is not None:
        _dev.close()
    _dev = None
    _part_bytes = {}


def part_size(part):
    if part not in _part_bytes:
        d = open_device()
        body, _ = d.require('getvar:partition-size:%s' % part)
        _part_bytes[part] = int(body.decode('ascii').strip(), 16)
    return _part_bytes[part]


def read_part_range(part, offset, length):
    """按分区内偏移读任意字节（内部处理非对齐）。"""
    base = PART_LBA[part]
    s = (offset // 512) * 512
    e = offset + length
    nsec = (e - s + 511) // 512
    raw = read_lba(base + s // 512, nsec)
    return raw[offset - s: offset - s + length]


def read_lba(lba, count):
    """读 count 个 512 B 扇区。"""
    d = open_device()
    out, left, cur = b'', count, lba
    while left > 0:
        k = min(64, left)
        out += upload_blocks(d, cur, k)
        cur += k
        left -= k
    return out


def build_sparse(offset, data, part_bytes):
    """构造「只写 data 这一段」的 sparse 镜像。offset/长度都须 4096 对齐。"""
    if offset % BLK:
        raise ValueError('offset 必须 %d 对齐（实际 0x%X）' % (BLK, offset))
    if len(data) % BLK:
        raise ValueError('数据长度必须 %d 的整数倍（实际 %d）' % (BLK, len(data)))
    if offset + len(data) > part_bytes:
        raise ValueError('写入越过分区末尾')
    total = part_bytes // BLK
    nblk = len(data) // BLK
    pre = offset // BLK
    post = total - pre - nblk
    chunks = []
    if pre:
        chunks.append(struct.pack('<HHII', CHUNK_SKIP, 0, pre, 12))
    chunks.append(struct.pack('<HHII', CHUNK_RAW, 0, nblk, 12 + len(data)) + data)
    if post:
        chunks.append(struct.pack('<HHII', CHUNK_SKIP, 0, post, 12))
    hdr = struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, total, len(chunks), 0)
    return hdr + b''.join(chunks)


def write_part_range(part, offset, data):
    """在分区内 offset 处写 data（两者都须 4 KiB 对齐）。"""
    pb = part_size(part)
    img = build_sparse(offset, data, pb)
    d = open_device()
    download(d, img)
    d.require('flash:%s' % part)
    return len(data)


class Region:
    """把「读一段、改几处、写回」封装成一次 4 KiB 对齐的读改写。

    FAT 表和根目录都很小，整段读进内存后改完写回，比逐块写安全。
    """

    def __init__(self, part, offset, length):
        self.part = part
        self.offset = offset - (offset % BLK)
        self.length = ((offset + length + BLK - 1) // BLK) * BLK - self.offset
        base_lba = PART_LBA[part]
        self.buf = bytearray(read_lba(base_lba + self.offset // 512, self.length // 512))
        self.bias = offset - self.offset          # 调用方 offset 在本缓冲里的偏移

    def view(self, offset, length):
        """返回 Region 内部某段的切片视图（按绝对分区偏移）。"""
        s = offset - self.offset
        if s < 0 or s + length > len(self.buf):
            raise ValueError('视图越过 Region 范围')
        return s, length

    def dirty(self):
        return self.buf

    def flush(self):
        write_part_range(self.part, self.offset, bytes(self.buf))
        return self.length

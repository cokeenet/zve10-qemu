#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 ZV-1F backup.ko 可查询的备份数据库 blob（放 QEMU bionz.c @0x20001000）。

schema 全部来自 backup.ko 反汇编（2026-10-26 实证，见 backup_ko.dis）：
  头 0x100: +0x04 cookie BE 0xAAAAAAAA / +0x08 writeComp BE 1 / +0x0c "BK4"
            +0x10 N1 = 页表项数(LE32)   ← Data_init/SearchId 用
            +0x20 checksum(LE32)=字节和(该字段清零后)  +0x28 ID1=1  +0x2c count(LE32)
  页表 @0x100: N1 × 6B { u16 LE count, u32 LE cum_base }   （id>>16 = 页号）
  槽表 @0x100+6*N1: × 6B { u16 LE 0, u32 LE v }            （id&0xffff = sub）
      v = 0xFFFFFFFF → 空槽；否则 v=(type<<24)|off
      type 1..254: size=type, 数据在 base+off
      （0x01120001 的记录用 type=20；0x01700000 用 type=4）
  数据: 跟在槽表后。

记录内容（libObj CInfraIndctrConf 装载器 0x206d04 实证）：
  id 0x01120001 (28B): "BIN!" + size=20 + count_A=0 + count_B=0 + "CNF!"
      （两道 magic 都过 → this+0x14 必初始化 → 不再 NULL 崩；phiIdNum=count_A=0）
  id 0x01700000 (4B):  setting=0（与 objset 补丁同值）
"""
import struct
import sys

HDR = 0x100
BASE_PHYS = 0x20001000

# 我们要的记录
REC_BIN = struct.pack('<IIIII',
                      0x214E4943 - 1,  # placeholder, replaced below
                      0, 0, 0, 0)
REC_BIN = (b'BIN!' +                    # +0x00 magic1
           struct.pack('<I', 28) +      # +0x04 size（== Backup_get_datasize，总长 28）
           struct.pack('<I', 0) +       # +0x08 count_A（phiIdNum）
           struct.pack('<I', 0) +       # +0x0C count_B
           b'CNF!' +                    # +0x10 magic2
           b'PTN!' +                    # +0x14 magic3
           b'FIN!')                     # +0x18 magic4
assert len(REC_BIN) == 28
REC_SET = struct.pack('<I', 0)          # 4 字节 setting = 0
REC_ULOG = b'\x00'                       # 1 字节：id 0x51000e（ulogio 要的，A/B 探针）

def build() -> bytes:
    # 页号
    PAGE_BIN, SUB_BIN = 0x112, 1        # id 0x01120001
    PAGE_SET, SUB_SET = 0x170, 0        # id 0x01700000
    N1 = max(PAGE_BIN, PAGE_SET) + 1    # = 0x171
    assert N1 == 0x171

    pages = [(0, 0)] * N1               # (count, cum_base)
    # 每个用到的页分配槽位；page 0x112 要能容纳 sub=1 → count=2（sub0 留空）
    slots = []                          # v 值
    def alloc(page, count):
        cum = len(slots)
        pages[page] = (count, cum)
        for _ in range(count):
            slots.append(0xFFFFFFFF)
        return cum
    PAGE_ULOG, SUB_ULOG = 0x51, 0x0e    # id 0x51000e（ulogio 探针）
    c51 = alloc(PAGE_ULOG, SUB_ULOG + 1)
    slots[c51 + SUB_ULOG] = None
    c112 = alloc(PAGE_BIN, 2)
    slots[c112 + SUB_BIN] = None        # 稍后填
    c170 = alloc(PAGE_SET, 1)
    slots[c170 + SUB_SET] = None
    # MonDrv::Init 的监视器固定区（libMWF 0x31334/0x31338 实证）。
    # 0x8F000000 落在 DDR1（0x80000000+256M）内，/dev/mem mmap 可达（上游同款修法）。
    PAGE_FA, SUB_FA_TOP, SUB_FA_SIZE = 0xFA, 0x28, 0x5D
    REC_FATOP = struct.pack('<I', 0x8F000000)
    REC_FASZ = struct.pack('<I', 0x00080000)
    c_fa = alloc(PAGE_FA, SUB_FA_SIZE + 1)
    slots[c_fa + SUB_FA_TOP] = None
    slots[c_fa + SUB_FA_SIZE] = None

    slot_off = HDR + 6 * N1
    data_off = slot_off + 6 * len(slots)
    off_set = data_off
    off_bin = data_off + len(REC_SET)

    slots[c112 + SUB_BIN] = (28 << 24) | off_bin
    slots[c170 + SUB_SET] = (4 << 24) | off_set
    off_ulog = off_bin + len(REC_BIN)
    slots[c51 + SUB_ULOG] = (1 << 24) | off_ulog
    off_fa_top = off_ulog + len(REC_ULOG)
    off_fa_size = off_fa_top + 4
    slots[c_fa + SUB_FA_TOP] = (4 << 24) | off_fa_top
    slots[c_fa + SUB_FA_SIZE] = (4 << 24) | off_fa_size

    total = off_fa_size + len(REC_FASZ)
    buf = bytearray(total)
    # 头
    struct.pack_into('<I', buf, 0x00, BASE_PHYS)     # DB 物理地址（Backup.bin +0 指向这里）
    struct.pack_into('>I', buf, 0x04, 0xAAAAAAAA)    # cookie
    struct.pack_into('>I', buf, 0x08, 1)             # writeComp
    buf[0x0C:0x10] = b'BK4\x00'
    struct.pack_into('<I', buf, 0x10, N1)            # ★ N1（LE32）
    struct.pack_into('>I', buf, 0x28, 1)             # ID1
    struct.pack_into('<I', buf, 0x2C, len(slots))    # 条目数
    # 页表
    for i, (cnt, cum) in enumerate(pages):
        struct.pack_into('<H', buf, HDR + 6 * i, cnt)
        struct.pack_into('<I', buf, HDR + 6 * i + 2, cum)
    # 槽表
    for i, v in enumerate(slots):
        struct.pack_into('<I', buf, slot_off + 6 * i + 2, v & 0xFFFFFFFF)
    # 数据
    buf[off_set:off_set + 4] = REC_SET
    buf[off_bin:off_bin + 28] = REC_BIN
    buf[off_ulog:off_ulog + 1] = REC_ULOG
    buf[off_fa_top:off_fa_top + 4] = REC_FATOP
    buf[off_fa_size:off_fa_size + 4] = REC_FASZ
    # checksum：+0x20 清零后字节和
    struct.pack_into('<I', buf, 0x20, 0)
    struct.pack_into('<I', buf, 0x20, sum(buf) & 0xFFFFFFFF)
    return bytes(buf)

def to_c_array(b: bytes) -> str:
    lines = []
    for i in range(0, len(b), 8):
        lines.append('            ' + ', '.join('0x%02x' % x for x in b[i:i+8]) + ',')
    return '\n'.join(lines)

if __name__ == '__main__':
    data = build()
    out = sys.argv[1] if len(sys.argv) > 1 else 'backup_db.bin'
    open(out, 'wb').write(data)
    print('wrote %s  %d bytes (0x%x)' % (out, len(data), len(data)))
    print('N1=0x171  pages: 0x112=(count2,cum0) 0x170=(count1,cum2)')
    print('slots@0x%x  data@0x%x  set@0x%x  bin@0x%x' %
          (HDR + 6 * 0x171, HDR + 6 * 0x171 + 18,
           HDR + 6 * 0x171 + 18, HDR + 6 * 0x171 + 22))
    print('checksum=0x%08x' % struct.unpack_from('<I', data, 0x20)[0])
    if len(sys.argv) > 2 and sys.argv[2] == '--c':
        print(to_c_array(data))

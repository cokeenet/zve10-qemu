#!/usr/bin/env python3
"""检查 backup_db 结构是否与 Data_SearchId 兼容"""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/g/WorkBuddy/zve10/work/db_host.bin'
try:
    d = open(path, 'rb').read()
except:
    print('文件不存在:', path); sys.exit(1)

print('文件大小: %d B (0x%x)' % (len(d), len(d)))
print('头 0x00-0x14:', d[:0x14].hex(' '))
print('  +0x00 phys_addr = %#x' % struct.unpack_from('<I', d, 0)[0])
print('  +0x04 cookie    = %#x' % struct.unpack_from('>I', d, 4)[0])
print('  +0x08 writeComp = %#x' % struct.unpack_from('>I', d, 8)[0])
print('  +0x0C BK magic  = %r' % d[0x0C:0x10])
print('  +0x10 N1        = %#x (%d)' % (struct.unpack_from('<I', d, 0x10)[0], struct.unpack_from('<I', d, 0x10)[0]))
N1 = struct.unpack_from('<I', d, 0x10)[0]
print('  +0x20 checksum  = %#x' % struct.unpack_from('<I', d, 0x20)[0])
print('  +0x28 ID1       = %#x' % struct.unpack_from('>I', d, 0x28)[0])
print('  +0x2C count     = %#x' % struct.unpack_from('<I', d, 0x2C)[0])

print()
print('页表 @0x100, N1=%d, 总长 %d B' % (N1, 6*N1))
slot_off = 0x100 + 6 * N1
print('槽表 @0x%x' % slot_off)

# 找 BIN! 记录
pos = d.find(b'BIN!')
print('BIN! 在 offset %d' % pos)
pos2 = d.find(b'CNF!')
print('CNF! 在 offset %d' % pos2)
pos3 = d.find(b'PTN!')
print('PTN! 在 offset %d' % pos3)
pos4 = d.find(b'FIN!')
print('FIN! 在 offset %d' % pos4)

# 检查关键页
for page, sub, name in [(0x112, 1, '0x01120001 BIN'), (0x170, 0, '0x01700000 SET')]:
    if page >= N1:
        print('%s: 页号 %d >= N1 %d → 找不到!' % (name, page, N1)); continue
    pt = 0x100 + 6 * page
    cnt = struct.unpack_from('<H', d, pt)[0]
    cum = struct.unpack_from('<I', d, pt + 2)[0]
    print('%s: 页 %d count=%d cum=%d' % (name, page, cnt, cum))
    if sub >= cnt:
        print('  子号 %d >= count %d → 找不到!' % (sub, cnt)); continue
    idx = cum + sub
    so = slot_off + 6 * idx
    v = struct.unpack_from('<I', d, so + 2)[0]
    typ = v >> 24
    off = v & 0xFFFFFF
    print('  槽[%d] v=%#010x → type=%d off=%#x' % (idx, v, typ, off))
    if v == 0xFFFFFFFF:
        print('  ★ 空槽!'); continue
    if typ == 255:
        print('  type=255, 需查尺寸表'); continue
    print('  记录数据 @0x%x: %s' % (off, d[off:off+min(typ,28)].hex(' ')))

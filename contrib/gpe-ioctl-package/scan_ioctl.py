"""扫描 libObj.dis 里所有 ioctl@plt 调用点，回溯 r1（request 号）与 r0、r2 的来源。
ARM/Thumb 混编，靠 objdump 文本解析（勿用 capstone 线性扫）。
"""
import re, struct
from pathlib import Path

DIS = Path('work/libObj.dis')
SO = Path('work/libObj.so')
data = SO.read_bytes()

line_re = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*)$')
sym_re = re.compile(r'^([0-9a-f]{8}) <(.+)>:')

lines = DIS.open('r', encoding='latin1', errors='replace').read().split('\n')
insns = []   # (idx, addr, mnem, ops, sym)
cur_sym = '?'
for i, ln in enumerate(lines):
    ms = sym_re.match(ln)
    if ms:
        cur_sym = '%s@%s' % (ms.group(2), ms.group(1))
        continue
    m = line_re.match(ln)
    if not m:
        continue
    insns.append((i, int(m.group(1), 16), m.group(3), m.group(4).strip(), cur_sym))

print('指令数', len(insns))

def lit_from_comment(ops):
    mm = re.search(r'\(([0-9a-f]+)\s*<', ops)
    return int(mm.group(1), 16) if mm else None

results = []
for k, (li, addr, mnem, ops, sym) in enumerate(insns):
    if mnem != 'blx' or 'ioctl@plt' not in ops:
        continue
    r1 = None; r1how = ''; r0 = None; r2 = None
    # 回溯最多 40 条
    for j in range(k - 1, max(k - 41, -1), -1):
        _, a2, mn, op, _ = insns[j]
        if mn.startswith('movw') and op.startswith('r1,'):
            if r1 is None:
                r1 = int(op.split('#')[-1].split(';')[0].strip(), 0) & 0xFFFF; r1how = 'movw@%x' % a2
        elif mn.startswith('movt') and op.startswith('r1,'):
            if r1 is not None and 'movt' not in r1how:
                r1 = (int(op.split('#')[-1].split(';')[0].strip(), 0) << 16) | (r1 & 0xFFFF); r1how += '+movt@%x' % a2
        elif mn.startswith('mov') and op.startswith('r1,') and '#' in op and r1 is None:
            r1 = int(op.split('#')[-1].split(';')[0].strip(), 0) & 0xFFFFFFFF; r1how = 'mov@%x' % a2
        elif mn.startswith('ldr') and op.startswith('r1,') and r1 is None:
            p = lit_from_comment(op)
            if p is not None and p + 4 <= len(data):
                r1 = struct.unpack_from('<I', data, p)[0]; r1how = 'ldr[p=%x]' % p
        if r0 is None and mn.startswith(('mov', 'ldr')) and op.startswith('r0,') and '#' not in op:
            r0 = 'ok'
        if r2 is None and mn.startswith(('mov', 'ldr', 'add')) and op.startswith('r2,') and '#' not in op:
            r2 = 'ok'
    results.append((addr, sym, r1, r1how))

print('\n== ioctl 调用点 ==')
for addr, sym, r1, how in results:
    if r1 is None:
        cmd = '???'
    else:
        cmd = '%08x' % r1
        if (r1 & 0xff000000) in (0x40000000, 0x80000000, 0xc0000000):
            d = (r1 >> 30) & 3; size = (r1 >> 16) & 0x3fff; typ = (r1 >> 8) & 0xff; nr = r1 & 0xff
            cmd += '  dir=%d size=%d type=%02x nr=%d' % (d, size, typ, nr)
    print('  %08x  %-34s  r1=%-8s %-26s' % (addr, sym, cmd, how))

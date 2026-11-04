"""扫描 libObj.dis 里所有 ioctl@plt 调用点：
  - 回溯 r1(请求号) 的 movw/movt 组合 或 ldr 字面量
  - 用手工函数边界（返回指令之后的地址）判定所在函数
  - 顺带记录 r2 是否为寄存器/常量，辅助推断入参大小
"""
import re, struct
from pathlib import Path

DIS = Path('work/libObj.dis')
SO = Path('work/libObj.so')
data = SO.read_bytes()

line_re = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*)$')
sym_re = re.compile(r'^([0-9a-f]{8}) <(.+)>:')

lines = DIS.open('r', encoding='latin1', errors='replace').read().split('\n')
insns = []
cur_sym = '?'
for ln in lines:
    ms = sym_re.match(ln)
    if ms:
        cur_sym = '%s@%s' % (ms.group(2), ms.group(1))
        continue
    m = line_re.match(ln)
    if not m:
        continue
    insns.append((int(m.group(1), 16), m.group(3), m.group(4).strip(), cur_sym))

print('指令数', len(insns))

RET = ('pop', 'bx', 'ldm', 'ldmia', 'mov', 'add')  # 只对 pop/bx 判定，下面细化
def is_ret(mn, op):
    if mn.startswith('pop') and 'pc' in op: return True
    if mn.startswith('bx') and op.strip() == 'lr': return True
    if mn.startswith(('ldm', 'ldmia')) and 'pc' in op: return True
    if mn.startswith('b') and mn not in ('b', ) : return False
    return False

starts = []
for i, (a, mn, op, sy) in enumerate(insns):
    if is_ret(mn, op) and i + 1 < len(insns):
        starts.append(insns[i + 1][0])
starts.sort()
import bisect
def func_start(a):
    k = bisect.bisect_right(starts, a) - 1
    return starts[k] if k >= 0 else 0

def imm_of(op):
    mm = re.search(r'#(-?0x[0-9a-f]+|-?\d+)', op)
    return int(mm.group(1), 0) if mm else None

def litaddr_of(op):
    mm = re.search(r'\(([0-9a-f]+)\s*<', op)
    return int(mm.group(1), 16) if mm else None

out = []
for k, (addr, mnem, ops, sym) in enumerate(insns):
    if mnem != 'blx' or 'ioctl@plt' not in ops:
        continue
    movw = movt = mv = ldrp = None
    r2info = ''
    for j in range(k - 1, max(k - 60, -1), -1):
        a2, mn, op, _ = insns[j]
        if op.startswith('r1,'):
            if mn.startswith('movw') and movw is None: movw = (a2, imm_of(op))
            elif mn.startswith('movt') and movt is None: movt = (a2, imm_of(op))
            elif mn in ('mov', 'movs', 'mov.w') and mv is None and imm_of(op) is not None:
                mv = (a2, imm_of(op))
            elif mn.startswith('ldr') and ldrp is None:
                ldrp = (a2, litaddr_of(op))
        if r2info == '' and op.startswith('r2,'):
            if imm_of(op) is not None: r2info = 'r2=#%d@%x' % (imm_of(op), a2)
            elif 'pc' in op and litaddr_of(op):
                p = litaddr_of(op)
                v = struct.unpack_from('<I', data, p)[0] if p + 4 <= len(data) else 0
                r2info = 'r2=lit[%x]=%#x@%x' % (p, v, a2)
            else:
                r2info = 'r2=reg@%x' % a2
    cmd = None; how = ''
    if movw and movw[1] is not None:
        cmd = movw[1] & 0xFFFF; how = 'movw@%x' % movw[0]
        if movt and movt[1] is not None:
            cmd |= (movt[1] & 0xFFFF) << 16; how += '+movt@%x' % movt[0]
        else:
            how += '(无movt?)'
    elif mv:
        cmd = mv[1]; how = 'mov@%x' % mv[0]
    elif ldrp and ldrp[1]:
        cmd = struct.unpack_from('<I', data, ldrp[1])[0]; how = 'lit[%x]' % ldrp[1]
    out.append((addr, func_start(addr), sym, cmd, how, r2info))

print('\n== ioctl 调用点 (%d) ==' % len(out))
for addr, fs, sym, cmd, how, r2 in out:
    if cmd is None:
        s = '???'
    elif (cmd & 0xff000000) in (0x40000000, 0x80000000, 0xc0000000) and (cmd & 0xffff) < 0x1000:
        d = (cmd >> 30) & 3; size = (cmd >> 16) & 0x3fff; typ = (cmd >> 8) & 0xff; nr = cmd & 0xff
        s = '%08x dir=%d size=%-3d type=%02x nr=0x%02x' % (cmd, d, size, typ, nr)
    else:
        s = '%08x' % cmd
    print('  call@%08x func@%08x  %-22s %-34s %-10s %s' % (addr, fs, s, how, r2, sym))

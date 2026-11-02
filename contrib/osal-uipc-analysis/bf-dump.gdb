# bf_dump.gdb — boot_factor 断点集 + 命中时寄存器/栈回溯
# 前提：$ab 已由 scan_dump.gdb 设成 appFw.so 的加载基址，且已 target remote 连上。
#
# 目的：App 在读完 boot_factor 后 2 秒内永久停摆（8.2 实测），
#       要拿到「谁调的 get_boot_factor / 它上层在等什么」。
#       ARM APCS 约定：fp 指向 {保存的旧fp, 返回地址}，故可从 $r7 逐帧回溯。

set pagination off
set confirm off
set architecture arm

python
import gdb

def rd32(a):
    try:
        return int.from_bytes(bytes(gdb.selected_inferior().read_memory(a, 4)), 'little')
    except Exception:
        return None

def reg(n):
    try:
        return int(gdb.parse_and_eval('$' + n)) & 0xffffffff
    except Exception:
        return 0

def get_base():
    try:
        return int(gdb.parse_and_eval('$ab')) & 0xffffffff
    except Exception:
        return 0

def tag(v):
    b = get_base()
    if v is None:
        return '??'
    if b and b <= v < b + 0x4000000:
        return '<appFw+%#x>' % (v - b)
    if 0x04000000 <= v < 0x04100000:
        return '<libpthread?>'
    if 0x04000000 <= v < 0x05000000:
        return '<lib区>'
    return ''

class DumpStack(gdb.Command):
    """打印寄存器 + fp 链回溯 + sp 起 64 字原始栈"""
    def __init__(self):
        super(DumpStack, self).__init__('dumpstack', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        fp = reg('r7'); sp = reg('sp'); lr = reg('lr'); pc = reg('pc')
        gdb.write('  --- regs ---\n')
        gdb.write('  r0=%08x r1=%08x r2=%08x r3=%08x\n'
                  % (reg('r0'), reg('r1'), reg('r2'), reg('r3')))
        gdb.write('  r4=%08x r5=%08x r6=%08x r7=%08x\n'
                  % (reg('r4'), reg('r5'), reg('r6'), reg('r7')))
        gdb.write('  r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x\n'
                  % (reg('r8'), reg('r9'), reg('r10'), reg('r11'), reg('r12')))
        gdb.write('  sp=%08x lr=%08x pc=%08x %s\n' % (sp, lr, pc, tag(pc)))
        gdb.write('  --- fp 链回溯 ---\n')
        f = fp
        for i in range(12):
            if f == 0 or f % 4 or f < 0x1000:
                gdb.write('  frame%-2d fp=%08x  (链断)\n' % (i, f)); break
            nxt = rd32(f); ret = rd32(f + 4)
            if ret is None:
                gdb.write('  frame%-2d fp=%08x  (读不到)\n' % (i, f)); break
            gdb.write('  frame%-2d fp=%08x ret=%08x %s\n' % (i, f, ret, tag(ret)))
            if nxt is None or nxt <= f or nxt % 4:
                gdb.write('  frame%-2d 下帧 fp=%08x 不合理，停\n' % (i + 1, nxt or 0)); break
            f = nxt
        gdb.write('  --- 栈上代码指针（sp .. sp+0x4000，只打命中项）---\n')
        hits = 0
        for off in range(0, 0x4000, 4):
            v = rd32(sp + off)
            if v is None:
                gdb.write('  sp+%04x: (读不到，停)\n' % off)
                break
            t = tag(v)
            if t:
                gdb.write('  sp+%04x: %08x %s\n' % (off, v, t))
                hits += 1
                if hits >= 60:
                    gdb.write('  ...（已打印 60 条，截断）\n')
                    break
        if hits == 0:
            gdb.write('  （16KB 内没有落进 appFw / 0x04 库区的指针）\n')

DumpStack()
end

# helper：把 32B boot_factor 写进 model（文件偏移 0x172a164）
hbreak *($ab + 0x172A164)
commands
  silent
  printf "HIT gbf_helper lr=%p\n", $lr
  continue
end

# get_boot_factor 入口（0x172a1ac）—— 这里 dump 能看到「谁调它」
hbreak *($ab + 0x172A1AC)
commands
  silent
  printf "\n=== HIT gbf_entry pc=%p lr=%p r7=%p ===\n", $pc, $lr, $r7
  dumpstack
  continue
end

# 成功路径日志点：r10=key r9=type r8=eecap r6=bis r5=attr r4=ext_mode
hbreak *($ab + 0x172A232)
commands
  silent
  printf "\n=== HIT gbf_SUCCESS key=%08x type=%08x eecap=%08x bis=%08x attr=%08x ext_mode=%08x lr=%p ===\n", $r10, $r9, $r8, $r6, $r5, $r4, $lr
  dumpstack
  continue
end

# 失败路径入口（patch 后应为死代码；命中=补丁未生效）
hbreak *($ab + 0x172A250)
commands
  silent
  printf "HIT gbf_FAILURE r7=%p lr=%p\n", $r7, $lr
  continue
end

# get_boot_factor 的三个调用者
hbreak *($ab + 0x1760100)
commands
  silent
  printf "HIT caller1 (0x1760100) lr=%p\n", $lr
  continue
end
hbreak *($ab + 0x1791880)
commands
  silent
  printf "HIT caller2 (0x1791880) lr=%p\n", $lr
  continue
end
hbreak *($ab + 0x17A1AE6)
commands
  silent
  printf "\n=== HIT caller3 (0x17a1ae6) lr=%p r7=%p ===\n", $lr, $r7
  dumpstack
  continue
end

# 启动键 dispatcher：tbh [pc, r5, lsl #1]，r5=key
hbreak *($ab + 0x1208934)
commands
  silent
  printf "HIT key_dispatch key=%08x\n", $r5
  continue
end

# Not support boot key
hbreak *($ab + 0x12090D8)
commands
  silent
  printf "HIT NotSupportBootKey key=%08x\n", $r5
  continue
end

# Application_Boot_Select 字符串引用点
hbreak *($ab + 0x4C5C04)
commands
  silent
  printf "HIT Application_Boot_Select lr=%p\n", $lr
  continue
end

continue

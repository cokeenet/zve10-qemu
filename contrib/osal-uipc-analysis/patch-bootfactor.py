#!/usr/bin/env python3
# patch_bootfactor.py — 在 appFw.so 里给 get_boot_factor 装"合法默认值"
#
# 原理：
#   get_boot_factor @ 0x172a1ac 调 UIPC(0x238a5b0) 问 upm.ko 要 boot_factor_t。
#   QEMU 里该调用返回非 0（Failure），随后 bne 进失败分支，boot 卡住。
#   本补丁把 0x172a1bc 的 bl 0x238a5b0 改成 bl 0x172a250（失败分支代码洞，
#   改后永不可达），洞里写入合法 boot_factor 并把 flag 清零，然后 bx lr。
#
# 结构体（32B，栈上 [r7+8..39]，进 model 的顺序）：
#   +0  key       = 0   （无按键 = 正常电源开机）
#   +4  type      = 2   （upm.ko k_upm_setup_boot_factor_default 的默认值）
#   +8  eecap     = 0
#   +12 ?         = 0
#   +16 bis       = 0
#   +20 attr      = 0
#   +24 ?         = 0
#   +28 ext_mode  = 0
import struct, sys, os

# 接受命令行参数指定树路径，默认 objset 树（向后兼容）
if len(sys.argv) > 1:
    PATH = os.path.join(sys.argv[1], 'lib', 'appFw.so')
else:
    PATH = '/tmp/zv1f_usr_mon_objset_tree/lib/appFw.so'

ADDR_BL   = 0x172a1bc          # bl 0x238a5b0 的地址
BL_ORIG   = bytes.fromhex('60f0f8d1')
ADDR_CAVE = 0x172a250          # 失败分支入口（改后死代码，当洞用）
CAVE_ORIG = bytes.fromhex('2e4c')  # ldr r4,[pc,#184]

def th_str(rt, rn, imm5):      # str Rt,[Rn,#imm5*4]  T1
    return 0x6000 | (imm5 << 6) | (rn << 3) | rt

cave = b''
cave += struct.pack('<H', 0x2000)          # movs r0, #0
cave += struct.pack('<H', 0x2102)          # movs r1, #2   (type)
cave += struct.pack('<H', th_str(0,7,1))   # str r0,[r7,#4]   flag=0
cave += struct.pack('<H', th_str(0,7,2))   # str r0,[r7,#8]   key=0
cave += struct.pack('<H', th_str(1,7,3))   # str r1,[r7,#12]  type=2
cave += struct.pack('<H', th_str(0,7,4))   # str r0,[r7,#16]  eecap=0
cave += struct.pack('<H', th_str(0,7,5))   # str r0,[r7,#20]
cave += struct.pack('<H', th_str(0,7,6))   # str r0,[r7,#24]  bis=0
cave += struct.pack('<H', th_str(0,7,7))   # str r0,[r7,#28]  attr=0
cave += struct.pack('<H', th_str(0,7,8))   # str r0,[r7,#32]
cave += struct.pack('<H', th_str(0,7,9))   # str r0,[r7,#36]  ext_mode=0
cave += struct.pack('<H', 0x4770)          # bx lr

def th_bl(src, dst):
    off = dst - (src + 4)
    v = off >> 1
    S = (v >> 24) & 1
    i1 = (v >> 23) & 1; i2 = (v >> 22) & 1
    imm10 = (v >> 12) & 0x3FF; imm11 = v & 0x7FF
    J1 = (1 ^ i1) ^ S if S else (i1 ^ 1)
    # I1 = NOT(J1 EOR S) -> J1 = NOT(I1) EOR S
    J1 = ((i1 ^ 1) & 1) ^ S
    J2 = ((i2 ^ 1) & 1) ^ S
    h1 = 0xF000 | (S << 10) | imm10
    h2 = 0xD000 | (J1 << 13) | (J2 << 11) | imm11
    return struct.pack('<HH', h1, h2)

with open(PATH, 'r+b') as f:
    f.seek(ADDR_BL); orig = f.read(4)
    assert orig == BL_ORIG, f'BL 指纹不符: {orig.hex()}'
    f.seek(ADDR_CAVE); orig2 = f.read(2)
    assert orig2 == CAVE_ORIG, f'洞指纹不符: {orig2.hex()}'
    f.seek(ADDR_CAVE); f.write(cave)
    f.seek(ADDR_BL);   f.write(th_bl(ADDR_BL, ADDR_CAVE))
print(f'patched: bl@{ADDR_BL:#x} -> cave@{ADDR_CAVE:#x} ({len(cave)}B), boot_factor={{key:0,type:2,rest:0}}')

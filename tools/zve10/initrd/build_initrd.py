#!/usr/bin/env python3
"""构建带 libnewmenu 的 initrd.img

方案：
  ① 复制原始 initrd
  ② 把 libnewmenu.so 放进去作为 /z.so
  ③ 补丁 /sbin/init：0xAE2F 处 17 字节
     "LT_ABORTREASON=0\0" → "LD_PRELOAD=/z.so\0"
  ④ 输出新的 initrd.img

为什么这条路能成：
  · init 的挂载列表里【没有 nflasha7 → /】
  · 所以 / 一直是 initramfs（内存盘）
  · 把库放进 initrd，init 立刻就能读到 /z.so
  · 不依赖任何分区挂载时机
"""
import struct, sys, hashlib, subprocess, shutil, os
from pathlib import Path

SRC_INITRD = Path('/tmp/cam_initrd.img')
LIB        = Path('/root/fw/oss/tools/zve10/menu/libnewmenu.so')
WORK       = Path('/tmp/initrd_build')
import os as _os
OUT        = Path('/root/fw/oss/tools/zve10/initrd/initrd_%s.img'
                  % ('safe' if _os.environ.get('SAFE_MODE','1')=='1' else 'hook'))

OLD = b"LT_ABORTREASON=0\x00"
NEW = b"LD_PRELOAD=/z.so\x00"
ADDR = 0xAE2F
DELTA = 0x8000

def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd,str), capture_output=True, text=True, **kw)

def main():
    assert len(OLD) == len(NEW)
    if not SRC_INITRD.exists():
        print("✗ 缺少 %s" % SRC_INITRD); return 1
    if not LIB.exists():
        print("✗ 缺少 %s" % LIB); return 1

    WORK.mkdir(exist_ok=True)
    img = WORK / 'initrd.img'
    if img.exists(): img.unlink()
    shutil.copy2(SRC_INITRD, img)
    print("✓ 复制 initrd: %d B" % img.stat().st_size)

    mnt = WORK / 'mnt'
    mnt.mkdir(exist_ok=True)

    # 挂载
    r = sh(['mount','-o','loop',str(img),str(mnt)])
    if r.returncode != 0:
        print("✗ 挂载失败: %s" % r.stderr); return 1
    print("✓ 挂载")

    try:
        # ① 放库
        dst_lib = mnt / 'z.so'
        shutil.copy2(LIB, dst_lib)
        os.chmod(dst_lib, 0o755)
        print("✓ 放库: /z.so (%d B)" % dst_lib.stat().st_size)

        # ①b safe mode 开关（第一阶段验证用）
        safe_flag = os.environ.get('SAFE_MODE', '1')
        if safe_flag == '1':
            safe = mnt / 'z_safe'
            safe.write_bytes(b'')
            print("✓ SAFE MODE 开关: /z_safe（只观测，不装 hook）")
        else:
            print("  SAFE MODE 关闭（会装 hook）")

        # ② 空间检查
        st = os.statvfs(mnt)
        free = st.f_bavail * st.f_frsize
        print("  剩余空间: %d B" % free)
        if free < 1024:
            print("  ⚠ 空间紧张")

        # ③ 补丁 init
        init = mnt / 'sbin' / 'init'
        data = bytearray(init.read_bytes())
        off = ADDR - DELTA
        cur = bytes(data[off:off+len(OLD)])
        print("✓ init @0x%X: %r" % (ADDR, cur))
        if cur != OLD:
            print("✗ 内容不匹配"); return 1
        data[off:off+len(NEW)] = NEW
        print("  → %r" % NEW)

        # 写回（用临时文件 + 覆盖）
        tmp = WORK / 'init.new'
        tmp.write_bytes(bytes(data))
        shutil.copy2(tmp, init)
        os.chmod(init, 0o555)
        print("✓ init 已修改, md5=%s" % hashlib.md5(bytes(data)).hexdigest())
    finally:
        sh(['umount', str(mnt)])

    # ④ 验证
    r = sh(['mount','-o','loop,ro',str(img),str(mnt)])
    if r.returncode != 0:
        print("✗ 验证挂载失败"); return 1
    try:
        z = mnt / 'z.so'
        i = mnt / 'sbin' / 'init'
        ok_lib = z.exists() and z.stat().st_size == LIB.stat().st_size
        d = i.read_bytes()
        ok_init = d[ADDR-DELTA:ADDR-DELTA+len(NEW)] == NEW
        print()
        print("═══ 验证 ═══")
        print("  /z.so:      %s (%d B)" % ("✓" if ok_lib else "✗", z.stat().st_size if z.exists() else 0))
        print("  init 补丁:  %s" % ("✓" if ok_init else "✗"))
        print("  init md5:   %s" % hashlib.md5(d).hexdigest())
        if not (ok_lib and ok_init):
            return 1
    finally:
        sh(['umount', str(mnt)])

    shutil.copy2(img, OUT)
    print()
    print("✓ 输出: %s (%d B)" % (OUT, OUT.stat().st_size))
    print("  md5: %s" % hashlib.md5(OUT.read_bytes()).hexdigest())
    return 0

if __name__ == '__main__':
    sys.exit(main())

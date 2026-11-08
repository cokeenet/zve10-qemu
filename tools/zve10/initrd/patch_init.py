#!/usr/bin/env python3
"""给 initrd.img 里的 /sbin/init 打补丁

目的：让 init 在启动 bootin（进而 im.elf）之前，把 LD_PRELOAD 设成我们的库。

原理：
  init 的 main 开头（0x9A3C）有一个 putenv 循环，遍历 .rodata 里的环境数组：
    [1] HOME=/
    [2] PATH=...
    [3] TOPRC=/etc/toprc
    [4] MALLOC_OPTIONS=gfffffffff
    [5] MALLOC_MMAP_THRESHOLD_=32768
    [6] MALLOC_TRIM_THRESHOLD_=32768
    [7] LT_ABORTREASON=0        ← 17 字节

  把 [7] 换成 "LD_PRELOAD=/z.so\0"（也是 17 字节），
  循环就会 putenv("LD_PRELOAD=/z.so")，
  bootin 及 im.elf 全都会继承。

改动：17 字节（0xAE2F - 0xAE3F）
"""
import struct, sys, hashlib, subprocess, shutil
from pathlib import Path

INITRD = Path('/tmp/cam_initrd.img')          # 原始 initrd
WORK   = Path('/tmp/initrd_patch')            # 工作目录
OLD    = b"LT_ABORTREASON=0\x00"              # 17 B
NEW    = b"LD_PRELOAD=/z.so\x00"              # 17 B
ADDR   = 0xAE2F
DELTA  = 0x8000

def main():
    if not INITRD.exists():
        print("✗ 缺少 %s" % INITRD)
        return 1
    assert len(OLD) == len(NEW), "长度必须相同"

    WORK.mkdir(exist_ok=True)
    img = WORK / 'initrd_work.img'
    if img.exists(): img.unlink()
    shutil.copy2(INITRD, img)
    print("✓ 复制 %s → %s" % (INITRD, img))

    # 挂载
    mnt = WORK / 'mnt'
    mnt.mkdir(exist_ok=True)
    subprocess.run(['mount', '-o', 'loop', str(img), str(mnt)], check=True)
    print("✓ 挂载到 %s" % mnt)

    try:
        init = mnt / 'sbin' / 'init'
        print("  init 原始 md5: %s" % hashlib.md5(init.read_bytes()).hexdigest())

        # 用 debugfs 直接改（避免挂载的只读问题）
        # 更简单：直接改底层文件——但 ext2 里的文件是分散的
        # 用 dd + debugfs write 更可靠：先把改好的 init 写到临时文件，再用 debugfs 替换
        data = bytearray(init.read_bytes())
        off = ADDR - DELTA
        cur = bytes(data[off:off+len(OLD)])
        print("  目标位置 0x%X: %r" % (ADDR, cur))
        if cur != OLD:
            print("  ✗ 内容不匹配")
            return 1
        data[off:off+len(NEW)] = NEW
        print("  → %r" % NEW)

        newinit = WORK / 'init.patched'
        newinit.write_bytes(bytes(data))
        print("✓ 写出 %s (%d B), md5=%s" % (newinit, len(data), hashlib.md5(data).hexdigest()))

        # 用 debugfs 替换（不挂载写）
        subprocess.run(['umount', str(mnt)], check=False)
        print("  ✓ 卸载")

        # debugfs 替换
        r = subprocess.run(
            ['debugfs', '-w', '-R', 'rm /sbin/init', str(img)],
            capture_output=True, text=True)
        print("  debugfs rm: %s" % (r.stdout + r.stderr).strip()[:80])

        r = subprocess.run(
            ['debugfs', '-w', '-R', 'write %s /sbin/init' % newinit, str(img)],
            capture_output=True, text=True)
        print("  debugfs write: %s" % (r.stdout + r.stderr).strip()[:80])

        # 验证
        subprocess.run(['mount', '-o', 'loop,ro', str(img), str(mnt)], check=True)
        got = (mnt / 'sbin' / 'init').read_bytes()
        subprocess.run(['umount', str(mnt)], check=False)
        ok = got[off:off+len(NEW)] == NEW
        print()
        print("验证: %s" % ("✓ 通过" if ok else "✗ 失败"))
        print("  md5: %s" % hashlib.md5(got).hexdigest())
        print("  @0x%X: %r" % (ADDR, got[off:off+24]))
        return 0 if ok else 1
    finally:
        subprocess.run(['umount', str(mnt)], check=False, capture_output=True)

if __name__ == '__main__':
    sys.exit(main())

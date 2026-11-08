#!/usr/bin/env python3
"""从相机（或本地备份）提取原始 initrd.img

优先级：
  ① /system/INITRD.IMG（相机在线时）
  ② /root/fw/zve10_backup/nflasha3.img（本地分区备份）
  ③ /tmp/cam_initrd.img（已提取的）

输出：/tmp/cam_initrd.img
"""
import struct, sys, subprocess, shutil, os
from pathlib import Path

OUT = Path('/tmp/cam_initrd.img')
NF3 = Path('/root/fw/zve10_backup/nflasha3.img')

def from_camera():
    """通过 agent 从相机拉 /system/INITRD.IMG"""
    try:
        import socket
        s = socket.create_connection(('10.42.0.158', 7777), timeout=10)
        s.settimeout(30)
        path = '/system/INITRD.IMG'
        d = path.encode()
        s.sendall(b'PULL %d\n' % len(d) + d)
        line = b''
        while not line.endswith(b'\n'): line += s.recv(1)
        if line.startswith(b'ERR'): return None
        n = int(line[5:])
        buf = b''
        while len(buf) < n: buf += s.recv(n - len(buf))
        s.close()
        return buf
    except Exception:
        return None

def from_backup():
    """从 nflasha3.img（FAT16）提取 INITRD.IMG"""
    if not NF3.exists(): return None
    with open(NF3,'rb') as f:
        boot = f.read(512)
        bps = struct.unpack_from('<H', boot, 11)[0]
        spc = boot[13]; rsvd = struct.unpack_from('<H', boot, 14)[0]
        nfats = boot[16]; rootE = struct.unpack_from('<H', boot, 17)[0]
        fatsz = struct.unpack_from('<H', boot, 22)[0]
        root_off = (rsvd + nfats*fatsz) * bps
        data_off = root_off + rootE*32
        f.seek(root_off); root = f.read(rootE*32)
        f.seek(rsvd*bps); fat = f.read(fatsz*bps)
        def nxt(c):
            off = c*2
            if off+2 > len(fat): return 0xFFF8
            return struct.unpack_from('<H', fat, off)[0]
        for i in range(0, len(root), 32):
            e = root[i:i+32]
            if e[0]==0: break
            if e[0]==0xE5 or e[11]==0x0F: continue
            short = e[:8].decode('latin1','replace').rstrip()
            ext = e[8:11].decode('latin1','replace').rstrip()
            name = (short + ('.'+ext if ext else '')).upper()
            if not name.startswith('INITRD'): continue
            cl = struct.unpack_from('<H', e, 26)[0]
            sz = struct.unpack_from('<I', e, 28)[0]
            out = b''; c = cl; guard = 0
            while 2 <= c < 0xFFF8 and len(out) < sz and guard < 10000:
                f.seek(data_off + (c-2)*spc*bps)
                out += f.read(spc*bps)
                c = nxt(c); guard += 1
            return out[:sz]
    return None

def main():
    if OUT.exists():
        print("已存在: %s (%d B)" % (OUT, OUT.stat().st_size))
        print("  md5: %s" % __import__('hashlib').md5(OUT.read_bytes()).hexdigest())
        return 0

    print("尝试从相机提取...")
    data = from_camera()
    src = 'camera'
    if not data:
        print("  相机不可用，改从分区备份提取...")
        data = from_backup()
        src = 'nflasha3.img'
    if not data:
        print("✗ 无法获取 initrd")
        return 1

    OUT.write_bytes(data)
    print("✓ 从 %s 提取: %s (%d B)" % (src, OUT, len(data)))
    import hashlib
    print("  md5: %s" % hashlib.md5(data).hexdigest())
    return 0

if __name__ == '__main__':
    sys.exit(main())

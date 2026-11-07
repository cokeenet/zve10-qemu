#!/usr/bin/env python3
"""最小 ext2 读取器 —— 只够定位并读一个文件。

相机 /usr 是 ext2 (bs=1024, inode_size=128, rev=1)。
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'dev'))
import fb  # noqa: E402


class Ext2:
    def __init__(self, part):
        self.part = part
        sb = fb.read_part_range(part, 1024, 1024)
        self.inodes = struct.unpack_from('<I', sb, 0)[0]
        self.blocks = struct.unpack_from('<I', sb, 4)[0]
        self.first_data = struct.unpack_from('<I', sb, 20)[0]
        self.log_bs = struct.unpack_from('<I', sb, 24)[0]
        self.bpg = struct.unpack_from('<I', sb, 32)[0]
        self.ipg = struct.unpack_from('<I', sb, 40)[0]
        self.inode_size = struct.unpack_from('<H', sb, 88)[0]
        self.rev = struct.unpack_from('<I', sb, 76)[0]
        self.bs = 1024 << self.log_bs
        self.gd_off = (self.first_data + 1) * self.bs

    def _blk(self, n, count=1):
        return fb.read_part_range(self.part, n * self.bs, count * self.bs)

    def inode(self, n):
        """读第 n 号 inode（1-based）。"""
        g = (n - 1) // self.ipg
        off = (n - 1) % self.ipg
        gd = fb.read_part_range(self.part, self.gd_off + g * 32, 32)
        itable = struct.unpack_from('<I', gd, 8)[0] if self.rev >= 1 else struct.unpack_from('<I', gd, 8)[0]
        pos = itable * self.bs + off * self.inode_size
        raw = fb.read_part_range(self.part, pos, self.inode_size)
        return dict(
            raw=raw,
            mode=struct.unpack_from('<H', raw, 0)[0],
            size=struct.unpack_from('<I', raw, 4)[0],
            block=[struct.unpack_from('<I', raw, 40 + 4 * i)[0] for i in range(15)],
        )

    def read_inode(self, n):
        """读 inode 的全部数据（只处理直接块 + 单级间接）。"""
        ino = self.inode(n)
        size = ino['size']
        out = b''
        # 12 个直接块
        for b in ino['block'][:12]:
            if not b:
                break
            out += self._blk(b)
            if len(out) >= size:
                return out[:size]
        # 单级间接
        ind = ino['block'][12]
        if ind and len(out) < size:
            ptrs = self._blk(ind)
            per = self.bs // 4
            for i in range(per):
                b = struct.unpack_from('<I', ptrs, i * 4)[0]
                if not b:
                    break
                out += self._blk(b)
                if len(out) >= size:
                    return out[:size]
        # 双级间接
        dind = ino['block'][13]
        if dind and len(out) < size:
            l1 = self._blk(dind)
            per = self.bs // 4
            for i in range(per):
                ind2 = struct.unpack_from('<I', l1, i * 4)[0]
                if not ind2:
                    break
                ptrs = self._blk(ind2)
                for j in range(per):
                    b = struct.unpack_from('<I', ptrs, j * 4)[0]
                    if not b:
                        break
                    out += self._blk(b)
                    if len(out) >= size:
                        return out[:size]
        return out[:size]

    def listdir(self, inode_n):
        data = self.read_inode(inode_n)
        out = []
        i = 0
        while i + 8 <= len(data):
            ino, rec, name_len = struct.unpack_from('<IHB', data, i)
            if rec == 0:
                break
            if ino != 0:
                name = data[i + 8:i + 8 + name_len].decode('latin1', 'replace')
                out.append((name, ino))
            i += rec
        return out

    def lookup(self, path):
        """路径 → inode 号。"""
        cur = 2  # 根目录
        for part in [p for p in path.split('/') if p]:
            found = None
            for name, ino in self.listdir(cur):
                if name == part:
                    found = ino
                    break
            if found is None:
                raise FileNotFoundError(path)
            cur = found
        return cur

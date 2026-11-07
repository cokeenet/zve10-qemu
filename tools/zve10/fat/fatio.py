#!/usr/bin/env python3
"""FAT12/FAT16 读写 —— 只处理相机用到的两种情况。

/setting = FAT12 (bps=512, spc=32, rsvd=1, nfats=1, rootE=512, fatsz=4)
/log     = FAT16 (bps=512, spc=32, rsvd=1, nfats=1, rootE=512, fatsz=125)
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'dev'))
import fb  # noqa: E402

LONG = 0x0F
DIRENT = 0x10
DELETED = 0xE5


class Volume:
    def __init__(self, part):
        self.part = part
        self.base = fb.PART_LBA[part]
        bpb = fb.read_lba(self.base, 1)
        if bpb[510:512] != b'\x55\xaa':
            raise ValueError('%s 没有 FAT 引导扇区签名' % part)
        self.bps = struct.unpack_from('<H', bpb, 11)[0]
        self.spc = bpb[13]
        self.rsvd = struct.unpack_from('<H', bpb, 14)[0]
        self.nfats = bpb[16]
        self.rootE = struct.unpack_from('<H', bpb, 17)[0]
        self.fatsz = struct.unpack_from('<H', bpb, 22)[0] or struct.unpack_from('<I', bpb, 36)[0]
        self.root_lba = self.base + self.rsvd + self.nfats * self.fatsz
        self.data_lba = self.root_lba + (self.rootE * 32) // self.bps
        self.fat_lba = self.base + self.rsvd
        self.cluster_bytes = self.spc * self.bps
        fs = bpb[54:62].decode('latin1', 'replace').strip('\x00 ')
        self.fstype = fs or 'FAT?'
        self.bits12 = self.fstype != 'FAT16'

    def __repr__(self):
        return ('<Volume %s %s base=0x%X bps=%d spc=%d root=0x%X data=0x%X fat=0x%X>'
                % (self.part, self.fstype, self.base, self.bps, self.spc,
                   self.root_lba, self.data_lba, self.fat_lba))

    def fat_bytes(self):
        return fb.read_lba(self.fat_lba, self.fatsz)

    def get_next(self, fat, cluster):
        if self.bits12:
            i = cluster + cluster // 2
            if i + 1 >= len(fat):
                return 0xFFF
            v = fat[i] | (fat[i + 1] << 8)
            return (v >> 4) if (cluster & 1) else (v & 0xFFF)
        i = cluster * 2
        return struct.unpack_from('<H', fat, i)[0] if i + 1 < len(fat) else 0xFFFF

    def set_next(self, fat, cluster, value):
        if self.bits12:
            i = cluster + cluster // 2
            if cluster & 1:
                fat[i] = (fat[i] & 0x0F) | ((value << 4) & 0xF0)
                fat[i + 1] = (value >> 4) & 0xFF
            else:
                fat[i] = value & 0xFF
                fat[i + 1] = (fat[i + 1] & 0xF0) | ((value >> 8) & 0x0F)
        else:
            struct.pack_into('<H', fat, cluster * 2, value)

    def eoc(self):
        return 0xFFF if self.bits12 else 0xFFFF

    def chain(self, fat, start):
        out, c, seen = [], start, set()
        while 2 <= c < (0xFF8 if self.bits12 else 0xFFF8):
            if c in seen:
                raise ValueError('FAT 链出现环：簇 %d' % c)
            seen.add(c)
            out.append(c)
            c = self.get_next(fat, c)
        return out

    def read_clusters(self, clusters):
        data = b''
        for c in clusters:
            data += fb.read_lba(self.data_lba + (c - 2) * self.spc, self.spc)
        return data

    def write_clusters(self, clusters, data):
        per = self.cluster_bytes
        for i, c in enumerate(clusters):
            chunk = data[i * per:(i + 1) * per]
            if len(chunk) < per:
                chunk += b'\x00' * (per - len(chunk))
            fb.write_lba(self.data_lba + (c - 2) * self.spc, chunk)

    def listdir(self, cluster=None):
        if cluster is None:
            raw = fb.read_lba(self.root_lba, (self.rootE * 32) // self.bps)
        else:
            fat = self.fat_bytes()
            raw = self.read_clusters(self.chain(fat, cluster))
        return parse_dir(raw)

    def entry(self, name):
        for e in self.listdir():
            if e['name'].lower() == name.lower():
                return e
        return None

    def read_file(self, name, cluster=None):
        e = self.entry(name) if cluster is None else None
        if e is None:
            raise FileNotFoundError(name)
        fat = self.fat_bytes()
        return self.read_clusters(self.chain(fat, e['cluster']))[:e['size']], e

    def free_clusters(self, count):
        fat = self.fat_bytes()
        total = (self.fatsz * self.bps * 8) // (12 if self.bits12 else 16)
        out = []
        for c in range(2, total):
            if self.get_next(fat, c) == 0:
                out.append(c)
                if len(out) >= count:
                    break
        return out


def parse_dir(raw):
    ents, pend = [], {}
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == DELETED:
            pend.clear()
            continue
        if e[11] == LONG:
            body = e[1:11] + e[14:26] + e[28:32]
            s = ''
            for j in range(0, len(body), 2):
                c = body[j] | (body[j + 1] << 8)
                if c in (0, 0xFFFF):
                    break
                s += chr(c)
            pend[e[0] & 0x3F] = s
            continue
        if e[0] == ord('.'):
            continue
        name = ''
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, '?') for k in range(1, m + 1))
            pend = {}
        short = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        ents.append(dict(
            offset=i, name=name or (short + ('.' + ext if ext else '')),
            short=short, ext=ext, attr=e[11],
            cluster=struct.unpack_from('<H', e, 26)[0],
            size=struct.unpack_from('<I', e, 28)[0],
        ))
    return ents

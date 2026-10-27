#!/usr/bin/env python3
"""fb2png.py — 帧缓冲 RGBA4444 → PNG
支持多种分辨率猜测，输出非零像素最多的那个版本。
"""
import struct
from pathlib import Path

W = Path('/mnt/g/WorkBuddy/zve10/work')

def rgba4444_to_rgba8888(data):
    """RGBA4444 (大端 bit 布局 R[15:12] G[11:8] B[7:4] A[3:0]) → RGBA8888"""
    out = bytearray(len(data) * 2)
    for i in range(0, len(data) - 1, 2):
        v = data[i] | (data[i+1] << 8)  # 小端读 uint16
        r = (v >> 12) & 0xF
        g = (v >> 8) & 0xF
        b = (v >> 4) & 0xF
        a = v & 0xF
        out[i*2]   = r * 17
        out[i*2+1] = g * 17
        out[i*2+2] = b * 17
        out[i*2+3] = a * 17
    return bytes(out)

def rgb565_to_rgba8888(data):
    """RGB565 → RGBA8888"""
    out = bytearray(len(data) * 2)
    for i in range(0, len(data) - 1, 2):
        v = data[i] | (data[i+1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[i*2]   = r * 255 // 31
        out[i*2+1] = g * 255 // 63
        out[i*2+2] = b * 255 // 31
        out[i*2+3] = 255
    return bytes(out)

def rgba8888_passthrough(data):
    return data

def write_png(path, w, h, rgba):
    """最小 PNG writer (无依赖)"""
    import zlib
    def chunk(ctype, data):
        c = ctype + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    raw = b''
    stride = w * 4
    for y in range(h):
        raw += b'\x00' + rgba[y*stride:(y+1)*stride]
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6))
    png += chunk(b'IEND', b'')
    path.write_bytes(png)

def analyze_and_dump(name, data):
    """尝试多种分辨率+格式，输出非零覆盖率最高的"""
    if len(data) < 64:
        print(f"  {name}: 太小 ({len(data)} B)")
        return
    results = []
    for fmt_name, fmt_fn in [('RGBA4444', rgba4444_to_rgba8888),
                              ('RGB565', rgb565_to_rgba8888),
                              ('RGBA8888', rgba8888_passthrough)]:
        bpp = 4 if fmt_name == 'RGBA8888' else 2
        for w, h in [(640,480),(800,600),(1024,768),(480,272),(320,240),
                     (720,480),(480,320),(800,480),(1024,600),(640,360)]:
            need = w * h * bpp
            if len(data) < need:
                continue
            chunk = data[:need]
            rgba = fmt_fn(chunk)
            # 统计非透明非黑像素
            visible = sum(1 for i in range(0, len(rgba), 4)
                         if rgba[i] > 10 or rgba[i+1] > 10 or rgba[i+2] > 10)
            total = w * h
            pct = 100 * visible // total
            results.append((pct, visible, fmt_name, w, h, rgba))
    if not results:
        print(f"  {name}: 无匹配分辨率")
        return
    results.sort(key=lambda x: -x[1])
    # 输出 top-3
    for pct, vis, fmt, w, h, rgba in results[:3]:
        outname = f"{name}_{fmt}_{w}x{h}.png"
        write_png(W / outname, w, h, rgba)
        print(f"  {name}: {fmt} {w}x{h} → {outname}  可见像素 {vis}/{w*h} ({pct}%)")
    # 也输出像素值样本
    d = data[:64]
    hexs = ' '.join(f'{b:02x}' for b in d)
    print(f"    前 64B: {hexs}")

for fname in ['fb_seg6.bin', 'fb_gpedst.bin', 'fb_gpures.bin', 'fb_alt.bin']:
    p = W / fname
    if p.exists():
        print(f"== {fname} ({p.stat().st_size} B) ==")
        analyze_and_dump(fname.replace('.bin',''), p.read_bytes())
    else:
        print(f"== {fname}: 不存在 ==")

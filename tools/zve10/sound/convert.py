#!/usr/bin/env python3
"""把任意 WAV 转成 ZV-E10 的 .bin 音效格式

目标格式（逆向所得）:
  · 8-bit signed PCM
  · 24000 Hz 单声道
  · 每 32 字节一块，首字节是递减标记（0x17, 0x16, 0x15, ...）
  · 实际样点 = 每块后 31 字节
"""
import wave, struct, sys
from pathlib import Path

TARGET_SR = 24000

def load_mono_16bit(path):
    w = wave.open(str(path), 'rb')
    ch = w.getnchannels(); sw = w.getsampwidth(); sr = w.getframerate()
    n = w.getnframes()
    raw = w.readframes(n)
    w.close()
    if sw == 2:
        s = struct.unpack('<%dh' % (len(raw)//2), raw)
    elif sw == 1:
        s = [(b-128)*256 for b in raw]
    elif sw == 4:
        s = [v >> 16 for v in struct.unpack('<%di' % (len(raw)//4), raw)]
    else:
        raise ValueError('unsupported sample width %d' % sw)
    if ch == 2:
        s = [(s[i]+s[i+1])//2 for i in range(0, len(s), 2)]
    elif ch > 2:
        s = [sum(s[i:i+ch])//ch for i in range(0, len(s), ch)]
    return list(s), sr

def resample(s, src_sr, dst_sr):
    if src_sr == dst_sr: return s
    ratio = src_sr / dst_sr
    out_n = int(len(s) / ratio)
    out = []
    for i in range(out_n):
        pos = i * ratio
        i0 = int(pos); frac = pos - i0
        v0 = s[i0] if i0 < len(s) else 0
        v1 = s[i0+1] if i0+1 < len(s) else 0
        out.append(int(v0 + (v1-v0)*frac))
    return out

def encode(s, mark_start=0x17):
    """16-bit → 8-bit signed，每 32 字节块首字节放标记"""
    # 归一化防止削波
    peak = max((abs(v) for v in s), default=0)
    if peak > 32000:
        scale = 32000.0 / peak
        s = [int(v * scale) for v in s]

    out = bytearray()
    mark = mark_start
    i = 0
    while i < len(s):
        chunk = s[i:i+31]
        b = bytearray(32)
        b[0] = mark & 0xFF
        for j, v in enumerate(chunk):
            # 16-bit signed → 8-bit signed（除以 256）
            v8 = v >> 8
            v8 = max(-128, min(127, v8))
            b[1+j] = v8 & 0xFF
        out += b
        i += 31
        mark = mark - 1 if mark > 0x08 else 0x17
    return bytes(out)

def convert(src, dst, trim_silence=True, max_len=None):
    s, sr = load_mono_16bit(src)
    print("  输入: %d 样点 @ %d Hz = %.3f 秒" % (len(s), sr, len(s)/sr))

    if trim_silence:
        # 去掉首尾静音（阈值 200）
        L = 0
        while L < len(s) and abs(s[L]) < 200: L += 1
        R = len(s)
        while R > L and abs(s[R-1]) < 200: R -= 1
        if R > L:
            s = s[L:R]
            print("  裁剪静音: %d 样点（%.3f 秒）" % (len(s), len(s)/sr))

    s = resample(s, sr, TARGET_SR)
    print("  重采样: %d 样点 @ %d Hz = %.3f 秒" % (len(s), TARGET_SR, len(s)/TARGET_SR))

    if max_len and len(s) > max_len:
        s = s[:max_len]
        print("  截断到 %d 样点" % len(s))

    data = encode(s)
    Path(dst).write_bytes(data)
    print("  输出: %s (%d B, %d 块)" % (dst, len(data), len(data)//32))
    return len(data)

if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else 'user_input.wav'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'Dslr_Emnt_FocusLock.NEW.bin'
    print("═══ 转换 %s → %s ═══" % (src, dst))
    convert(src, dst)

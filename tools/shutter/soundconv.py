#!/usr/bin/env python3
"""ZV-E10 快门音效格式转换器（离线，不连相机）。

相机短音效格式（/usr/share/avsystemsound/*.bin）：
    每 32 字节为一个块；块首字节高 4 位 = predictor(0/1)，低 4 位 = shift(0..8)
    其后 31 字节 = 有符号残差
    解码：previous = residual * (1<<shift) + (15/16 if predictor else 0) * previous

原厂 Dslr_Emnt_Shutter_Sha.bin / Dslr_Emnt_10FPS_Kasha.bin：
    1216 字节 = 38 块 = 1178 样本 @24 kHz ≈ 0.049 s
    sha256 = 957ae8939f66bff6fdc1810cf854dbed51306d34cf6c8838e944bb5ed78be3fd

用法：
    soundconv.py decode <in.bin> <out.wav>
    soundconv.py encode <in.wav> <out.bin>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audio_codec as ac


def decode(src, dst):
    raw = Path(src).read_bytes()
    vals = ac.decode_native(raw)
    wav = ac._preview_wav(vals)
    Path(dst).write_bytes(wav)
    print('%s -> %s  %d 样本  %.3f s  %d B' %
          (src, dst, len(vals), len(vals) / ac.TARGET_RATE, len(wav)))


def encode(src, dst):
    prepared = ac.prepare_audio(Path(src))
    data = prepared.data
    Path(dst).write_bytes(data)
    print('%s -> %s  %d B（%d 块）  sha256=%s' %
          (src, dst, len(data), len(data) // 32, prepared.sha256[:24]))


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('decode', 'encode'):
        sys.exit(__doc__)
    (decode if sys.argv[1] == 'decode' else encode)(sys.argv[2], sys.argv[3])

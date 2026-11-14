"""Bounded, offline WAV/native-BIN preparation for this ZV-E10 sound tool.

The native block model is derived from the local experimental_codec.py research:
32 bytes hold one control byte and 31 signed residuals. New WAVs always use
predictor zero, the same encoding family as the 7,232-byte camera-tested clip.
Predictor-one decoding and the 24 kHz playback rate remain a software model;
preview WAVs are not bit-exact recordings of the camera's hardware decoder.
Only the standard library is used. No USB, process, network or camera imports.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
import math
from pathlib import Path
import struct
import sys
import wave

TARGET_RATE = 24_000
MAX_SECONDS = 1.0
MAX_SOURCE_BYTES = 5 * 1024 * 1024
BLOCK_BYTES = 32
BLOCK_SAMPLES = 31
MAX_ENCODED_BYTES = math.ceil(TARGET_RATE / 62) * 64  # 24,832, including zero padding.
NATIVE_SHA256 = '957ae8939f66bff6fdc1810cf854dbed51306d34cf6c8838e944bb5ed78be3fd'
RECORDED_SHA256 = 'e63f7eb54d2810f584db0d1ae224e8898e3ff3b97dc2abfc30c59a79018f5021'


@dataclass(frozen=True)
class PreparedAudio:
    data: bytes
    preview_wav: bytes
    title: str
    duration_seconds: float
    source_path: str
    sha256: str
    note: str


def asset_path(name: str) -> Path:
    """Locate a bundled asset both from source and a PyInstaller executable."""
    if name not in ('native-shutter.bin', 'c2-recorded-shutter.bin', 'synthetic-demo.wav', 'source_manifest.json'):
        raise ValueError('未知的内置音效文件。')
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent)) / 'assets' / name


def asset_available(name: str) -> bool:
    """Third-party sound BINs are optional and omitted from the public source."""
    try:
        return asset_path(name).is_file()
    except ValueError:
        return False


def _read_bounded(path: Path) -> tuple[Path, bytes]:
    path = Path(path).expanduser().resolve(strict=True)
    if not path.is_file():
        raise ValueError('请选择一个 WAV 或 BIN 文件。')
    if path.stat().st_size > MAX_SOURCE_BYTES:
        raise ValueError('音频文件不能超过 5 MB。')
    with path.open('rb') as stream:
        raw = stream.read(MAX_SOURCE_BYTES + 1)
    if not raw:
        raise ValueError('音频文件为空。')
    if len(raw) > MAX_SOURCE_BYTES:
        raise ValueError('音频文件不能超过 5 MB。')
    return path, raw


def decode_native(data: bytes) -> list[float]:
    """Research preview decoder; floating predictor history precedes WAV clipping."""
    if not data or len(data) % BLOCK_BYTES:
        raise ValueError('BIN 必须包含完整的 32 字节音频块。')
    values: list[float] = []
    previous = 0.0
    for at in range(0, len(data), BLOCK_BYTES):
        predictor, shift = data[at] >> 4, data[at] & 15
        if predictor not in (0, 1) or shift > 8:
            raise ValueError(f'BIN 在字节 {at} 包含不支持的音频块。')
        coefficient = 15 / 16 if predictor else 0
        for byte in data[at + 1:at + BLOCK_BYTES]:
            residual = byte if byte < 128 else byte - 256
            previous = residual * (1 << shift) + coefficient * previous
            values.append(previous)
    return values


def encode_predictor_zero(samples: list[float]) -> bytes:
    """Choose the least-squared-error exponent per block, never predictor one."""
    if not samples or len(samples) > TARGET_RATE or any(
            not math.isfinite(value) or abs(value) > 32767 for value in samples):
        raise ValueError('音频样本为空、过长或超出可编码范围。')
    padded = list(samples) + [0.0] * (-len(samples) % (2 * BLOCK_SAMPLES))
    output = bytearray()
    for at in range(0, len(padded), BLOCK_SAMPLES):
        target = padded[at:at + BLOCK_SAMPLES]
        best_error, best_block = math.inf, b''
        for shift in range(9):
            scale = 1 << shift
            quantized = [max(-128, min(127, round(value / scale))) for value in target]
            error = sum((value - integer * scale) ** 2 for value, integer in zip(target, quantized))
            if error < best_error:
                best_error = error
                best_block = bytes([shift] + [value & 255 for value in quantized])
        output.extend(best_block)
    return bytes(output)


def _read_pcm_wav(raw: bytes) -> tuple[list[float], int]:
    """Strict RIFF parser rejects compressed/float audio and truncated chunks."""
    if len(raw) < 12 or raw[:4] != b'RIFF' or raw[8:12] != b'WAVE':
        raise ValueError('不是有效的 PCM WAV 文件。')
    if struct.unpack_from('<I', raw, 4)[0] != len(raw) - 8:
        raise ValueError('WAV 文件长度不完整或包含额外数据。')
    fmt = pcm = None
    at = 12
    while at < len(raw):
        if at + 8 > len(raw):
            raise ValueError('WAV 数据块头不完整。')
        tag, size = struct.unpack_from('<4sI', raw, at)
        start, end = at + 8, at + 8 + size
        if end > len(raw):
            raise ValueError('WAV 数据块不完整。')
        if tag == b'fmt ':
            if fmt is not None:
                raise ValueError('WAV 存在重复的格式数据块。')
            fmt = raw[start:end]
        elif tag == b'data':
            if pcm is not None:
                raise ValueError('不支持包含多个音频数据块的 WAV。')
            pcm = raw[start:end]
        # Some common PCM writers omit the pad byte on the final odd data
        # chunk. All declared samples are present, so this is not truncation.
        padding = 0 if tag == b'data' and end == len(raw) else size & 1
        if end + padding > len(raw):
            raise ValueError('WAV 数据块的对齐字节缺失。')
        at = end + padding
    if fmt is None or len(fmt) < 16 or pcm is None:
        raise ValueError('WAV 缺少格式或音频数据。')
    encoding, channels, rate, byte_rate, alignment, bits = struct.unpack_from('<HHIIHH', fmt)
    if encoding == 0xfffe:
        pcm_guid = bytes.fromhex('0100000000001000800000aa00389b71')
        if (len(fmt) < 40 or struct.unpack_from('<H', fmt, 16)[0] != 22
                or struct.unpack_from('<H', fmt, 18)[0] != bits or fmt[24:40] != pcm_guid):
            raise ValueError('只支持整数 PCM 的扩展 WAV 格式。')
    elif encoding != 1:
        raise ValueError('只支持未压缩的整数 PCM WAV；不支持压缩或浮点 WAV。')
    elif len(fmt) != 16 and (len(fmt) < 18 or struct.unpack_from('<H', fmt, 16)[0] != len(fmt) - 18):
        raise ValueError('WAV 格式扩展长度不一致。')
    if channels not in (1, 2) or bits not in (8, 16, 24, 32):
        raise ValueError('WAV 需要单声道或双声道，位深为 8、16、24 或 32 位。')
    if not 8000 <= rate <= 96000:
        raise ValueError('WAV 采样率需要在 8,000–96,000 Hz 之间。')
    width = bits // 8
    if alignment != channels * width or byte_rate != rate * alignment:
        raise ValueError('WAV 格式中的采样长度不一致。')
    if not pcm or len(pcm) % alignment:
        raise ValueError('WAV 音频为空或最后一个采样帧不完整。')
    frames = len(pcm) // alignment
    if frames > rate:
        raise ValueError('音效最长为 1 秒；请先剪辑文件，工具不会自动截断。')
    values = []
    scale = 2.0 ** (16 - bits)
    for frame in range(0, len(pcm), alignment):
        total = 0.0
        for channel in range(channels):
            sample_at = frame + channel * width
            sample = (pcm[sample_at] - 128 if width == 1 else
                      int.from_bytes(pcm[sample_at:sample_at + width], 'little', signed=True))
            total += sample * scale
        values.append(max(-32767.0, min(32767.0, total / channels)))
    return values, rate


def resample_to_24k(samples: list[float], source_rate: int) -> list[float]:
    """Hann-windowed sinc low-pass before rate conversion; no peak normalization.

    Cutoff is 94% of the lower Nyquist frequency and the support spans at least
    18 low-pass lobes. Finite-file edge weights are normalized to preserve DC.
    This intentionally trades a small upper-band rolloff for reduced aliasing.
    """
    if source_rate == TARGET_RATE:
        return list(samples)
    count = max(1, (len(samples) * TARGET_RATE + source_rate // 2) // source_rate)
    cutoff = 0.94 * min(1.0, TARGET_RATE / source_rate)
    radius = math.ceil(18 / cutoff)
    phases: dict[int, list[tuple[int, float]]] = {}
    output = []
    for index in range(count):
        center, remainder = divmod(index * source_rate, TARGET_RATE)
        weights = phases.get(remainder)
        if weights is None:
            fraction = remainder / TARGET_RATE
            weights = []
            for delta in range(-radius + 1, radius + 1):
                distance = delta - fraction
                if abs(distance) >= radius:
                    continue
                x = math.pi * cutoff * distance
                sinc = 1.0 if abs(x) < 1e-12 else math.sin(x) / x
                weight = cutoff * sinc * (0.5 + 0.5 * math.cos(math.pi * distance / radius))
                weights.append((delta, weight))
            # Avoid retaining tens of thousands of phase kernels for odd rates.
            if len(phases) < 512:
                phases[remainder] = weights
        value = weight_sum = 0.0
        for delta, weight in weights:
            at = center + delta
            if 0 <= at < len(samples):
                value += samples[at] * weight
                weight_sum += weight
        converted = value / weight_sum if weight_sum else 0.0
        output.append(max(-32767.0, min(32767.0, converted)))
    return output


def _preview_wav(samples: list[float]) -> bytes:
    # A legal predictor-one BIN can exceed PCM16; saturate only the preview.
    pcm = b''.join(struct.pack('<h', max(-32768, min(32767, round(value)))) for value in samples)
    target = io.BytesIO()
    with wave.open(target, 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(TARGET_RATE)
        output.writeframes(pcm)
    return target.getvalue()


def _prepare_bin(raw: bytes, path: Path, title: str, note: str) -> PreparedAudio:
    if not raw or len(raw) % 64:
        raise ValueError('BIN 文件长度需要是 64 字节的整数倍。')
    if len(raw) > MAX_ENCODED_BYTES:
        raise ValueError('BIN 超过 1 秒音效允许的最大长度（24,832 字节）。')
    samples = decode_native(raw)
    if len(samples) > TARGET_RATE:
        if any(value != 0 for value in samples[TARGET_RATE:]):
            raise ValueError('BIN 包含超过 1 秒的非静音数据。')
        # Leave the binary unchanged; omit alignment-only zeros in the preview.
        samples = samples[:TARGET_RATE]
    return PreparedAudio(raw, _preview_wav(samples), title, len(samples) / TARGET_RATE,
                         str(path), hashlib.sha256(raw).hexdigest(), note)


def prepare_audio(path: Path) -> PreparedAudio:
    path, raw = _read_bounded(path)
    if path.suffix.lower() == '.bin':
        return _prepare_bin(raw, path, path.stem, '原始 BIN 保留不变。电脑试听使用研究解码模型，可能与机身扬声器不同。')
    if path.suffix.lower() != '.wav':
        raise ValueError('请选择 WAV 或 BIN 文件；其他格式请先转换为 PCM WAV。')
    samples, rate = _read_pcm_wav(raw)
    duration = len(samples) / rate
    converted = resample_to_24k(samples, rate)
    data = encode_predictor_zero(converted)
    preview = decode_native(data)[:len(converted)]
    note = '已转换为单声道 24 kHz，保留原音量；电脑试听可能与机身扬声器不同。'
    if rate != TARGET_RATE:
        note += ' 重采样已使用低通滤波。'
    return PreparedAudio(data, _preview_wav(preview), path.stem, duration, str(path),
                         hashlib.sha256(data).hexdigest(), note)


def _prepare_builtin(name: str, expected_hash: str, title: str, note: str) -> PreparedAudio:
    if not asset_available(name):
        raise ValueError('此版本未附带该音效，请选择自己的 WAV 或 BIN 文件，或使用合成示例。')
    path, raw = _read_bounded(asset_path(name))
    if hashlib.sha256(raw).hexdigest() != expected_hash:
        raise ValueError('内置音效校验失败，请重新解压完整的软件包。')
    return _prepare_bin(raw, path, title, note)


def prepare_native_builtin() -> PreparedAudio:
    return _prepare_builtin('native-shutter.bin', NATIVE_SHA256, '原厂电子快门音效',
                            '来自 A7C II 原厂资源，与 ZV-E10 原厂音效字节相同；不是机械快门录音。试听为软件解码。')


def prepare_recorded_builtin() -> PreparedAudio:
    return _prepare_builtin('c2-recorded-shutter.bin', RECORDED_SHA256, 'A7C II 机械快门录音',
                            '来自公开机械快门录音，已在这台 ZV-E10 上试听确认；不是索尼原厂音效文件。')


def prepare_demo_builtin() -> PreparedAudio:
    """An original generated short click, not a Sony sound or a C2 recording."""
    prepared = prepare_audio(asset_path('synthetic-demo.wav'))
    return PreparedAudio(prepared.data, prepared.preview_wav, '合成示例短音（非 C2）',
                         prepared.duration_seconds, prepared.source_path, prepared.sha256,
                         '本项目原创合成短音，用于演示转换和试听；不是索尼或 A7C II 的声音。')

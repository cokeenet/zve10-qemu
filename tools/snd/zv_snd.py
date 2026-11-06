#!/usr/bin/env python3
"""Decode/encode Sony ZV-series 8-bit delta-coded sound assets (.bin).

Format (reverse-engineered, verified by correlation test):
  file body (after 4-byte header) = 8-bit signed DELTA samples
  PCM[n] = sum(delta[0..n])   (first-order accumulator)
  header = 4 bytes; values vary per file (likely loop/volume/flags)

Evidence: adjacent-sample correlation of raw body = 0.31 (noise-like),
after accumulation = 0.992 (smooth) => delta coding confirmed.

SysSound.bin is a *container*: several sounds concatenated, each starting
with its own 4-byte header (e.g. 17 00 00 01 / 07 00 00 00).

Usage:
  zv_snd.py sections <in.bin>
  zv_snd.py decode   <in.bin> <out.wav> [--rate 48000] [--index N]
  zv_snd.py encode   <in.wav> <out.bin> [--header 17000001]
"""
import struct, sys, wave

HDRS = (b'\x17\x00\x00\x01', b'\x17\x00\x00\x00', b'\x07\x00\x00\x00')


def parse_sections(data):
    hs = []
    for pat in HDRS:
        i = 0
        while True:
            i = data.find(pat, i)
            if i < 0:
                break
            hs.append(i)
            i += 1
    return sorted(set(hs))


def delta_to_pcm(body, clip=True):
    acc = 0
    out = []
    for b in body:
        v = b - 256 if b > 127 else b
        acc += v
        if clip:
            if acc > 127:
                acc = 127
            elif acc < -128:
                acc = -128
        out.append(acc)
    return out


def pcm_to_delta(samples):
    prev = 0
    out = bytearray()
    for s in samples:
        d = s - prev
        if d > 127:
            d = 127
        elif d < -128:
            d = -128
        out.append((d + 256) & 0xFF)
        prev = s
    return bytes(out)


def pcm_to_wav(samples8, path, rate):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(1)
    w.setframerate(rate)
    w.writeframes(bytes((s + 128) & 0xFF for s in samples8))
    w.close()


def wav_to_pcm(path):
    w = wave.open(path, 'rb')
    if w.getsampwidth() != 1 or w.getnchannels() != 1:
        raise SystemExit("need 8-bit mono WAV (convert first)")
    data = w.readframes(w.getnframes())
    w.close()
    return [b - 128 for b in data], w.getframerate()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    data = open(sys.argv[2], 'rb').read()

    if cmd == 'sections':
        hs = parse_sections(data)
        print("container size = %d, sections = %d" % (len(data), len(hs)))
        for k, o in enumerate(hs):
            end = hs[k + 1] if k + 1 < len(hs) else len(data)
            print("  #%02d @%#07x  hdr=%s  body=%d" % (k, o, data[o:o + 4].hex(), end - o - 4))
        return 0

    if cmd == 'decode':
        rate = 48000
        if '--rate' in sys.argv:
            rate = int(sys.argv[sys.argv.index('--rate') + 1])
        idx = 0
        if '--index' in sys.argv:
            idx = int(sys.argv[sys.argv.index('--index') + 1])
        hs = parse_sections(data)
        if len(hs) > 1:
            start = hs[idx]
            end = hs[idx + 1] if idx + 1 < len(hs) else len(data)
            body = data[start + 4:end]
            print("section #%d @%#x hdr=%s body=%d" % (idx, start, data[start:start + 4].hex(), len(body)))
        else:
            body = data[4:]
        pcm = delta_to_pcm(body)
        pcm_to_wav(pcm, sys.argv[3], rate)
        print("wrote %s (%d samples, %d Hz, 8-bit mono)" % (sys.argv[3], len(pcm), rate))
        return 0

    if cmd == 'encode':
        hdr = bytes.fromhex('17000001')
        if '--header' in sys.argv:
            hdr = bytes.fromhex(sys.argv[sys.argv.index('--header') + 1])
        pcm, rate = wav_to_pcm(sys.argv[2])
        body = pcm_to_delta(pcm)
        open(sys.argv[3], 'wb').write(hdr + body)
        print("wrote %s (%d bytes body, %d Hz)" % (sys.argv[3], len(body), rate))
        return 0

    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main())

#!/bin/bash
# run_screendump: 用 QEMU screendump 截 VIP 显示输出
set -e
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-95}

cd /tmp
(
  sleep "$SECS"
  echo "screendump /tmp/vip_screen.ppm"
  sleep 2
  echo "quit"
) | timeout $((SECS + 60)) "$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/gs_u0.log -serial file:/tmp/gs_u1.log \
    -serial file:/tmp/gs_u2.log -serial file:/tmp/gs_u3.log \
    -serial file:/tmp/gs_u4.log \
    -monitor stdio \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00100000" \
    > /tmp/gs.stderr 2>&1 || true

if [ -f /tmp/vip_screen.ppm ]; then
  echo "screendump 成功: $(stat -c%s /tmp/vip_screen.ppm) B"
  cp /tmp/vip_screen.ppm "$W/"
  # PPM 转 PNG
  python3 - <<'PY'
import struct
from pathlib import Path
W = Path('/mnt/g/WorkBuddy/zve10/work')
d = (W / 'vip_screen.ppm').read_bytes()
# PPM P6 header
if d[:2] != b'P6':
    print('不是 PPM P6 格式')
    exit(1)
# 解析 header
i = 2
vals = []
while len(vals) < 3:
    while i < len(d) and d[i] in b' \t\n\r':
        i += 1
    if d[i:i+1] == b'#':
        while i < len(d) and d[i] != ord('\n'):
            i += 1
        continue
    j = i
    while j < len(d) and d[j] not in b' \t\n\r':
        j += 1
    vals.append(int(d[i:j]))
    i = j
w, h, maxval = vals
i += 1  # 跳过最后一个空白
pixels = d[i:]
print(f'PPM: {w}x{h} maxval={maxval}, {len(pixels)} B 像素数据')
# 转 PNG
import zlib
def chunk(ctype, data):
    c = ctype + data
    return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
rgba = bytearray(w * h * 4)
for p in range(w * h):
    rgba[p*4]   = pixels[p*3]
    rgba[p*4+1] = pixels[p*3+1]
    rgba[p*4+2] = pixels[p*3+2]
    rgba[p*4+3] = 255
raw = b''
for y in range(h):
    raw += b'\x00' + bytes(rgba[y*w*4:(y+1)*w*4])
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 6))
png += chunk(b'IEND', b'')
(W / 'vip_screen.png').write_bytes(png)
print(f'PNG: vip_screen.png ({w}x{h})')
# 统计非黑像素
nz = sum(1 for p in range(w*h) if pixels[p*3] > 5 or pixels[p*3+1] > 5 or pixels[p*3+2] > 5)
print(f'非黑像素: {nz}/{w*h} ({100*nz//(w*h)}%)')
PY
else
  echo "screendump 失败"
  grep -a 'screendump\|error\|vip\|VIP\|console' /tmp/gs.stderr | tail -10
fi

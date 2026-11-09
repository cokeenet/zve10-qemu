#!/bin/bash
# 用 socket 串口，允许注入按键
set -euo pipefail
NAME="${1:?}"; TIMEOUT="${2:-120}"
QEMU=/mnt/build/m2qemu/qsrc/build/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui2/arch/arm/boot/Image
OUT="/tmp/qemu-$NAME"; UNIT="zve10-$NAME"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME (socket serial) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel "$KERNEL" \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial "file:$OUT/serial0.log" \
  -serial "pipe:$OUT/serial1.pipe" \
  -monitor "unix:$OUT/mon.sock,server=on,wait=off" \
  -no-reboot
sleep 3
systemctl is-active --quiet "$UNIT" || { echo "✗ 失败"; exit 1; }
echo "✓ 运行中，serial1 socket: $OUT/serial1.sock"
E=0
while [ "$E" -lt "$TIMEOUT" ]; do sleep 5; E=$((E+5)); systemctl is-active --quiet "$UNIT" || break; done
systemctl stop "$UNIT" 2>/dev/null || true; sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "✓ 完成 $(stat -c %s "$OUT/serial0.log" 2>/dev/null) B"

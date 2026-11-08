#!/bin/bash
set -euo pipefail
NAME="${1:?}"
TIMEOUT="${2:-90}"
QEMU=/mnt/build/m2qemu/qsrc/build/qemu-system-arm
KERNEL=/mnt/build/m2qemu/work/kbuild-ui2/arch/arm/boot/Image
CPIO=/tmp/ir/native-ui.cpio
OUT="/tmp/qemu-$NAME"
UNIT="zve10-$NAME"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== 启动 $NAME (带 -initrd) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -nodefaults -no-user-config \
  -M zve10 -cpu cortex-a9 -smp 1 -m 1024 \
  -kernel "$KERNEL" -initrd "$CPIO" \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 rdinit=/init loglevel=5' \
  -display none -serial "file:$OUT/serial0.log" \
  -monitor none -no-reboot
sleep 3
systemctl is-active --quiet "$UNIT" || { echo "✗ 启动失败"; tail -10 "$OUT/qemu.err"; exit 1; }
echo "✓ 运行中"
E=0
while [ "$E" -lt "$TIMEOUT" ]; do sleep 5; E=$((E+5)); systemctl is-active --quiet "$UNIT" || break; done
systemctl stop "$UNIT" 2>/dev/null || true
sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "✓ 清理完成  $(stat -c %s "$OUT/serial0.log" 2>/dev/null) B"

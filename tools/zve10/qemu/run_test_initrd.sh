#!/bin/bash
set -euo pipefail
NAME="${1:?}"; CPIO="${2:?}"; TIMEOUT="${3:-40}"
QEMU=/mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui/arch/arm/boot/Image
OUT="/tmp/qemu-$NAME"; UNIT="zve10-$NAME"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME: -initrd $CPIO ($(stat -c %s $CPIO) B) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel "$KERNEL" -initrd "$CPIO" \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial "file:$OUT/serial0.log" -serial "file:$OUT/serial1.log" \
  -monitor none -no-reboot
sleep 3
systemctl is-active --quiet "$UNIT" || { echo "  启动失败"; exit 1; }
E=0
while [ "$E" -lt "$TIMEOUT" ]; do sleep 5; E=$((E+5)); systemctl is-active --quiet "$UNIT" || break; done
systemctl stop "$UNIT" 2>/dev/null || true; sleep 1
systemctl reset-failed "$UNIT" 2>/dev/null || true
R=$(grep -a "INITRD" "$OUT/serial0.log" 2>/dev/null | head -1)
echo "  结果: ${R:-无 INITRD 消息}"

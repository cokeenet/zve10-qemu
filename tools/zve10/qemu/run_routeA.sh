#!/bin/bash
# 路线 A：上游 native-window-v14 的完整方案
set -euo pipefail
NAME="${1:?}"; TIMEOUT="${2:-180}"
QEMU=/mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui/arch/arm/boot/Image
CPIO=/mnt/build/routeA/ui-final.cpio
OUT="/tmp/qemu-$NAME"; UNIT="zve10-$NAME"
SER="$OUT/serial.sock"; MON="$OUT/mon.sock"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME (route A: native-window-v14) ==="
echo "  kernel: $(md5sum $KERNEL | cut -c1-16)  cpio: $(stat -c %s $CPIO) B"
systemd-run --unit="$UNIT" --collect --no-block \
  --setenv=ZVE10_QUIET_GPE=1 \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -nodefaults -no-user-config \
  -M zve10 -cpu cortex-a9 -smp "${SMP:-4}" -m 1024 \
  -kernel "$KERNEL" -initrd "$CPIO" \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial "unix:$SER,server=on,wait=on" \
  -qmp "unix:$MON,server=on,wait=off" -monitor none \
  -no-reboot
sleep 2
timeout "$TIMEOUT" python3 /tmp/uphost.py "$SER" "$MON" "$OUT" || true
systemctl stop "$UNIT" 2>/dev/null || true; sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "✓ 清理完成"

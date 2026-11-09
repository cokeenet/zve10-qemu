#!/bin/bash
# 完全按上游配置：单 serial wait=on + cfg.uart=1 + smp 1
set -euo pipefail
NAME="${1:?}"; TIMEOUT="${2:-160}"
QEMU=/mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui2/arch/arm/boot/Image
OUT="/tmp/qemu-$NAME"; UNIT="zve10-$NAME"
SER="$OUT/serial.sock"; QMP="$OUT/qmp.sock"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME (upstream-exact) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -nodefaults -no-user-config \
  -M zve10 -cpu cortex-a9 -smp "${SMP:-4}" -m 1024 \
  -kernel "$KERNEL" \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial "unix:$SER,server=on,wait=on" \
  -monitor "unix:$QMP,server=on,wait=off" -no-reboot
echo "  QEMU 已启动（wait=on 会阻塞到我们连上 serial）"
sleep 2
# 立即连 serial，让 QEMU 继续
timeout "$TIMEOUT" python3 /tmp/uphost.py "$SER" "$QMP" "$OUT" || true
systemctl stop "$UNIT" 2>/dev/null || true; sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "✓ 清理完成"

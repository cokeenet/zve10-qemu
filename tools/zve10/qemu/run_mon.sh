#!/bin/bash
# 用 QEMU monitor pmemsave 直接导 guest 物理内存
set -euo pipefail
NAME="${1:?}"; TIMEOUT="${2:-150}"
QEMU=/mnt/build/m2qemu/qsrc/build/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui2/arch/arm/boot/Image
OUT="/tmp/qemu-$NAME"; UNIT="zve10-$NAME"
MON="$OUT/qmp.sock"
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME (monitor pmemsave) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax=3G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel "$KERNEL" \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial "file:$OUT/serial0.log" -serial "file:$OUT/serial1.log" \
  -qmp "unix:$MON,server=on,wait=off" -D "$OUT/qemu-debug.log" \
  -no-reboot
sleep 3
systemctl is-active --quiet "$UNIT" || { echo "✗ 失败"; exit 1; }
echo "✓ 运行中"

# 等 UI 起来
E=0
while [ "$E" -lt 60 ]; do
  sleep 5; E=$((E+5))
  if [ -f "$OUT/serial0.log" ] && grep -q "UI_AFTER_SLEEP" "$OUT/serial0.log" 2>/dev/null; then
    echo "  UI 已启动（${E}s）"; break
  fi
done
sleep 6

# 用 monitor 抓三个候选物理地址
python3 /tmp/qmpcap.py "$MON" "$OUT"
echo "=== 停止 ==="
systemctl stop "$UNIT" 2>/dev/null || true; sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "✓ 清理完成"

#!/bin/bash
# ZV-E10 QEMU 实验启动器
#
# 硬性约束（用户要求）：
#   · 必须用 systemd-run --unit --collect --property=MemoryMax 启动
#   · 不留残留 qemu 进程
#   · 每次实验独立 unit 名，结束后清理
#
# 用法: ./run.sh <实验名> [超时秒数]
set -euo pipefail

NAME="${1:?需要实验名，如 vfat-mmap}"
TIMEOUT="${2:-180}"

QEMU=/mnt/build/m2qemu/qsrc/build/qemu-system-arm
KERNEL=/mnt/build/m2qemu/work/kbuild-ui2/arch/arm/boot/Image
OUT="/tmp/qemu-$NAME"
UNIT="zve10-$NAME"
MEM_MAX="3G"

[ -x "$QEMU" ]   || { echo "✗ 缺少 qemu: $QEMU"; exit 1; }
[ -f "$KERNEL" ] || { echo "✗ 缺少内核: $KERNEL"; exit 1; }

# 清掉同名旧 unit
systemctl stop "$UNIT" 2>/dev/null || true
systemctl reset-failed "$UNIT" 2>/dev/null || true

rm -rf "$OUT"; mkdir -p "$OUT"

echo "=== 启动 $NAME (unit=$UNIT, MemoryMax=$MEM_MAX, timeout=${TIMEOUT}s) ==="
systemd-run --unit="$UNIT" --collect --no-block \
  --property=MemoryMax="$MEM_MAX" \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" \
  -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel "$KERNEL" \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial "file:$OUT/serial0.log" -serial "file:$OUT/serial1.log" \
  -monitor none -no-reboot

sleep 3
if ! systemctl is-active --quiet "$UNIT"; then
    echo "✗ 启动失败"; tail -20 "$OUT/qemu.err" 2>/dev/null; exit 1
fi
echo "✓ 运行中，日志: $OUT/serial0.log"
echo

# 等超时或提前满足条件
ELAPSED=0
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    sleep 5; ELAPSED=$((ELAPSED+5))
    if ! systemctl is-active --quiet "$UNIT"; then
        echo "=== unit 已自行退出（${ELAPSED}s）==="
        break
    fi
    printf "\r  等待 %ds / %ds  日志 %d B" "$ELAPSED" "$TIMEOUT" "$(stat -c %s "$OUT/serial0.log" 2>/dev/null || echo 0)"
done
echo

echo "=== 停止 $UNIT ==="
systemctl stop "$UNIT" 2>/dev/null || true
sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true

if systemctl is-active --quiet "$UNIT"; then
    echo "⚠ unit 仍在运行，强制 kill"
    systemctl kill -s SIGKILL "$UNIT" 2>/dev/null || true
    sleep 1
fi

# 确认无残留
if pgrep -f "qemu-system-arm.*$NAME" >/dev/null 2>&1; then
    echo "⚠ 仍有残留，按名字 kill"
    pkill -9 -f "qemu-system-arm.*$NAME" 2>/dev/null || true
fi

echo "✓ 清理完成"
echo "  日志: $OUT/serial0.log ($(stat -c %s "$OUT/serial0.log" 2>/dev/null || echo 0) B)"

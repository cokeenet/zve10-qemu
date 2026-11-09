#!/bin/bash
# Route A: upstream native-window-v14 recipe, single instance, always cleans up.
set -uo pipefail
NAME="${1:-routeA1}"; TIMEOUT="${2:-180}"
QEMU=/mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm
KERNEL=/mnt/build/ref-chocotan-zve10/work/kbuild-ui/arch/arm/boot/Image
CPIO=/mnt/build/routeA/ui-final.cpio
ROOT=/mnt/build/m2qemu
OUT="$ROOT/out/$NAME"; UNIT="zve10-$NAME"
SER="$OUT/serial.sock"; MON="$OUT/mon.sock"

# single instance: stop every zve10 unit, then sweep by real exe basename
systemctl stop 'zve10-*' 2>/dev/null || true
systemctl reset-failed 'zve10-*' 2>/dev/null || true
for p in /proc/[0-9]*; do
  [ -r "$p/exe" ] || continue
  b=$(basename "$(readlink "$p/exe" 2>/dev/null || true)")
  case "$b" in qemu-system-arm*) kill -9 "${p##*/}" 2>/dev/null || true ;; esac
done
sleep 1
rm -rf "$OUT"; mkdir -p "$OUT"
echo "=== $NAME ==="
echo "  kernel=$(sha256sum "$KERNEL" | cut -c1-12) cpio=$(stat -c %s "$CPIO")B"

systemd-run --unit="$UNIT" --collect --no-block \
  --setenv=ZVE10_QUIET_GPE=1 \
  --property=MemoryMax=4G \
  --property=StandardError="file:$OUT/qemu.err" \
  --property=StandardOutput="file:$OUT/qemu.out" \
  "$QEMU" -nodefaults -no-user-config \
  -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel "$KERNEL" -initrd "$CPIO" \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial "unix:$SER,server=on,wait=on" \
  -qmp "unix:$MON,server=on,wait=off" -monitor none -no-reboot

sleep 3
timeout "$TIMEOUT" python3 /tmp/uphost.py "$SER" "$MON" "$OUT" > "$OUT/host.log" 2>&1
RC=$?
systemctl stop "$UNIT" 2>/dev/null || true
sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
PID=$(systemctl show -p MainPID --value "$UNIT" 2>/dev/null || echo 0)
echo "  host_rc=$RC unit_MainPID=$PID"
for p in /proc/[0-9]*; do
  [ -r "$p/exe" ] || continue
  b=$(basename "$(readlink "$p/exe" 2>/dev/null || true)")
  case "$b" in qemu-system-arm*) echo "  RESIDUAL ${p##*/}"; kill -9 "${p##*/}" 2>/dev/null || true ;; esac
done
echo "  done"

#!/usr/bin/env bash
# Run one bounded QEMU session with the widget-list probe and record every
# artifact binding. Single instance, systemd resource limits, cleanup at exit.
set -euo pipefail

ROOT=/mnt/build/m2qemu
OUT=$ROOT/out/fd2
UNIT=zve10-fd2
IMG=$ROOT/work/kbuild-ui2/arch/arm/boot/Image
QEMU=$ROOT/qsrc/build/qemu-system-arm

[ -x "$QEMU" ] || { echo "missing qemu" >&2; exit 2; }
[ -f "$IMG" ] || { echo "missing Image" >&2; exit 2; }
[ ! -e "$OUT" ] || { echo "output exists: $OUT" >&2; exit 2; }

# Stop any zve10 unit and any residual qemu by /proc exe basename.
for u in $(systemctl list-units --all --no-legend 2>/dev/null | awk '/zve10-/{print $1}'); do
    systemctl stop "$u" 2>/dev/null || true
    systemctl reset-failed "$u" 2>/dev/null || true
done
for p in /proc/[0-9]*; do
    pid=${p#/proc/}
    [ "$pid" = "$$" ] && continue
    [ -r "$p/exe" ] || continue
    exe=$(readlink "$p/exe" 2>/dev/null || true)
    case "$exe" in
        */qemu-system-arm|*/qemu-system-arm.real) kill -TERM "$pid" 2>/dev/null || true ;;
    esac
done
sleep 2
remain=0
for p in /proc/[0-9]*; do
    [ -r "$p/exe" ] || continue
    exe=$(readlink "$p/exe" 2>/dev/null || true)
    case "$exe" in */qemu-system-arm|*/qemu-system-arm.real) remain=$((remain+1)) ;; esac
done
[ "$remain" -eq 0 ] || { echo "residual qemu=$remain" >&2; exit 3; }

mkdir -p "$OUT"
sha256sum "$IMG" "$QEMU" "$ROOT/work/initramfs-ui/usr/lib/argtrace.so" \
    "$ROOT/stage-current/ui.cpio" > "$OUT/artifact-sha256.txt"

systemd-run --unit="$UNIT" --collect --no-block \
    --property=MemoryMax=4G --property=MemoryAccounting=yes \
    --working-directory="$ROOT" \
    --setenv=ZVE10_FRAME_OUT="$OUT/frame.raw" \
    --property=StandardError=file:$OUT/qemu.err \
    --property=StandardOutput=file:$OUT/qemu.out \
    "$QEMU" -M zve10 -cpu cortex-a9 -smp 1 -m 1024 \
    -kernel "$IMG" \
    -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
    -display none -serial "file:$OUT/serial0.log" -serial "file:$OUT/serial1.log" \
    -monitor none -no-reboot >/dev/null 2>&1

for i in $(seq 1 20); do
    PID=$(systemctl show -p MainPID --value "$UNIT" 2>/dev/null || true)
    [ -n "$PID" ] && [ "$PID" != 0 ] && break
    sleep 1
done
echo "pid=$PID" > "$OUT/qpid.txt"

for i in $(seq 1 120); do
    state=$(systemctl is-active "$UNIT" 2>/dev/null || true)
    [ "$state" = active ] || break
    grep -aq "FORCE_DRAW" "$OUT/serial0.log" "$OUT/serial0.log" 2>/dev/null && echo "wlist_seen_at=$i" >> "$OUT/launch.txt"
    sleep 1
done

systemctl stop "$UNIT" 2>/dev/null || true
sleep 2
systemctl reset-failed "$UNIT" 2>/dev/null || true
echo "state=$(systemctl is-active "$UNIT" 2>/dev/null || true)" > "$OUT/result.txt"
echo "finished=$(date -Is)" >> "$OUT/result.txt"
printf 'OUT=%s PID=%s\n' "$OUT" "$PID"

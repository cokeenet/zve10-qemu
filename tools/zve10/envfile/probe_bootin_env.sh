#!/bin/sh
# One-shot, read-only decision probe for the delivery route.
#
# Purpose: read the environment of the bootin child that THIS boot's init
# started. That single fact decides whether /etc/ld.so.preload can ever reach
# im.elf, or whether every boot is resuming from the startup image.
#
# Usage: sh probe_bootin_env.sh <camera-ip>
# Writes nothing; safe to run immediately after the camera comes online.

set -e
IP="${1:?usage: sh probe_bootin_env.sh <camera-ip>}"
HERE=$(cd "$(dirname "$0")" && pwd)
CAM="$HERE/../../payload/agent/cam.py"

python3 "$CAM" --ip "$IP" run '
BB=/bin/busybox
echo ===BOOTIN_ENV_BEGIN===
echo "--- boot id / uptime ---"
$BB cat /proc/sys/kernel/random/boot_id
$BB cat /proc/uptime

echo "--- init is pid 1? ---"
$BB cat /proc/1/comm
$BB cat /proc/1/cmdline | $BB wc -c

echo "--- bootin children of init, with environ ---"
found=0
for p in /proc/[0-9]*; do
  c=$($BB cat $p/comm 2>/dev/null)
  [ "$c" = "bootin.elf" ] || continue
  found=$((found+1))
  echo "BOOTIN pid=${p#/proc/}"
  echo "  ppid=$($BB cut -d" " -f4 $p/stat 2>/dev/null)"
  echo "  starttime=$($BB cut -d" " -f22 $p/stat 2>/dev/null)"
  echo "  environ:"
  $BB cat $p/environ 2>/dev/null | $BB tr "\000" "\n" | $BB sed "s/^/    /"
done
echo "bootin_count=$found"

echo "--- im.elf environ (for comparison) ---"
for p in /proc/[0-9]*; do
  c=$($BB cat $p/comm 2>/dev/null)
  [ "$c" = "im.elf" ] || continue
  echo "IMELF pid=${p#/proc/}"
  $BB cat $p/environ 2>/dev/null | $BB tr "\000" "\n" | $BB sed "s/^/    /"
  echo "  libnewmenu_maps=$($BB grep -c libnewmenu $p/maps 2>/dev/null)"
done

echo "--- preload file as the loader would read it ---"
$BB cat /etc/ld.so.preload
echo "--- /usr library in place? ---"
$BB ls -la /usr/lib/libnewmenu_usr.so 2>&1
$BB md5sum /usr/lib/libnewmenu_usr.so 2>&1

echo "--- resume state ---"
$BB cat /proc/ssboot/state
$BB cat /proc/ssboot/mode
$BB cat /proc/ssboot/resmode
echo ===BOOTIN_ENV_END===
' 2>&1

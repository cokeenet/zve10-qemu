#!/bin/sh
# 安全启动方式：不杀 bootin、不改 dmode/preload，只在自己进程里带 LD_PRELOAD
# 用法（在相机 ash 里）：sh /log/.launch_direct.sh
LOG=/log/nm-direct.log
exec >> $LOG 2>&1
echo "=== DIRECT LAUNCH $(cat /proc/uptime) ==="

STAGE=$(cat /log/nm-menu-stage 2>/dev/null || echo 1)
echo "stage=$STAGE"
busybox rm -f /log/nm-menu.log

# 完整复刻 im.elf 的启动环境
export HOME=/
export TERM=linux
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
export TOPRC=/etc/toprc
export MALLOC_OPTIONS=gfffffffff
export MALLOC_MMAP_THRESHOLD_=32768
export MALLOC_TRIM_THRESHOLD_=32768
export LT_ABORTREASON=0

# ★ 关键：只在这个进程里注入我们的库
export LD_PRELOAD=/log/menu_ui.so

cd /usr/share/app || exit 1
echo "cwd=$(pwd)  LD_PRELOAD=$LD_PRELOAD"

/usr/bin/im.elf debug s g r u l i a lazy-global > /log/im-direct.log 2>&1 &
PID=$!
echo "im.elf launched pid=$PID"

sleep 25
echo "=== AFTER 25s ==="
for d in /proc/[0-9]*/comm; do
  c=$(cat $d 2>/dev/null)
  case "$c" in im.elf|bootin*) echo "  $c: $(echo $d|sed 's|/proc/||;s|/comm||')";; esac
done
echo "=== nm-menu.log ==="
cat /log/nm-menu.log 2>/dev/null
echo "=== END ==="

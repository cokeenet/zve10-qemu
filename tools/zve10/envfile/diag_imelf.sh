#!/bin/sh
# 只读诊断：im.elf 到底有没有加载我们的库。
# 用法: sh diag_imelf.sh <相机IP>
#
# 这个脚本只读，不写任何文件。
IP="${1:?用法: sh diag_imelf.sh <相机IP>}"
HERE=$(cd "$(dirname "$0")" && pwd)
CAM="$HERE/../../payload/agent/cam.py"

python3 "$CAM" --ip "$IP" run '
BB=/bin/busybox

echo "===== 1. 找出所有 im.elf 进程 ====="
N=0
for p in /proc/[0-9]*; do
  c=$($BB cat $p/comm 2>/dev/null)
  if [ "$c" = "im.elf" ]; then
    N=$((N+1))
    pid=${p#/proc/}
    echo "--- im.elf pid=$pid ---"
    echo "  ppid     : $($BB cut -d" " -f4 $p/stat 2>/dev/null)"
    echo "  libnewmenu hits: $($BB grep -c libnewmenu $p/maps 2>/dev/null)"
    echo "  zve10_agent hits: $($BB grep -c zve10_agent $p/maps 2>/dev/null)"
    echo "  /etc 下的库      : $($BB grep -c "/etc/" $p/maps 2>/dev/null)"
  fi
done
echo "  im.elf 进程总数: $N"

echo
echo "===== 2. 所有进程里有几个加载了 libnewmenu ====="
T=0
for p in /proc/[0-9]*; do
  h=$($BB grep -c libnewmenu $p/maps 2>/dev/null)
  [ "${h:-0}" -gt 0 ] 2>/dev/null && {
    T=$((T+1))
    [ $T -le 8 ] && echo "  pid=${p#/proc/} comm=$($BB cat $p/comm 2>/dev/null) hits=$h"
  }
done
echo "  加载总数: $T"

echo
echo "===== 3. preload 文件 ====="
$BB cat /etc/ld.so.preload
echo "--- 字节数 ---"
$BB wc -c < /etc/ld.so.preload

echo
echo "===== 4. 日志文件（三个候选位置）====="
for L in /log/newmenu.log /tmp/newmenu.log /etc/newmenu.log; do
  if [ -f "$L" ]; then
    echo "--- $L ($($BB wc -c < $L) B) ---"
    $BB cat "$L"
  fi
done

echo
echo "===== 5. 挂载顺序 ====="
$BB grep -E " /(etc|log|tmp|usr|setting) " /proc/mounts

echo
echo "===== 6. /setting/mode/preload 是否存在 ====="
$BB ls -la /setting/mode/preload 2>&1
$BB cat /setting/mode/preload 2>&1

echo
echo "===== END ====="
' 2>&1

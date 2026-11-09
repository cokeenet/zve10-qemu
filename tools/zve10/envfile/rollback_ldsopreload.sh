#!/bin/sh
# 回滚 ld.so.preload 部署
# 用法: sh rollback_ldsopreload.sh <相机IP>
set -e
IP="${1:?用法: sh rollback_ldsopreload.sh <相机IP>}"
HERE=$(cd "$(dirname "$0")" && pwd)
CAM="$HERE/../../payload/agent/cam.py"
cam() { python3 "$CAM" --ip "$IP" "$@"; }

echo "=========================================="
echo "  回滚 $IP"
echo "=========================================="
cam run '
set -e
if [ -f /etc/ld.so.preload.orig ]; then
    cp /etc/ld.so.preload.orig /etc/ld.so.preload
    echo "已恢复 ld.so.preload.orig"
else
    printf "" > /etc/ld.so.preload
    echo "无备份，清空 ld.so.preload"
fi
rm -f /etc/libnewmenu.so
sync
echo "--- 当前 ld.so.preload ---"
cat /etc/ld.so.preload
echo "--- /etc/libnewmenu.so ---"
ls -la /etc/libnewmenu.so 2>&1
' 2>&1 | sed 's/^/  /'
echo
echo "  重启生效"

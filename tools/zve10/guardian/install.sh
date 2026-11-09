#!/bin/sh
# 在相机上安装 guardian
# 前提：/etc 可写（已挂载），二进制已推到 /etc/guardian

set -e
G=/etc/guardian

echo "=== 检查 ==="
[ -x "$G" ] || { echo "缺少 $G"; exit 1; }
[ -f /etc/ld.so.preload ] || { echo "缺少 /etc/ld.so.preload"; exit 1; }
echo "  preload:"
cat /etc/ld.so.preload | sed 's/^/    /'

echo "=== 启动 guardian ==="
# 先确保 off 标记不存在
rm -f /etc/guardian.off
# 后台启动
"$G" &
sleep 2

echo "=== 状态 ==="
if [ -f /log/guardian.log ]; then
    tail -20 /log/guardian.log | sed 's/^/  /'
else
    echo "  日志未生成"
fi

echo
echo "=== 当前 im.elf ==="
busybox ps | busybox grep -c "[i]m.elf" || echo "  0"

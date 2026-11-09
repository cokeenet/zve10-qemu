#!/bin/sh
# 禁用 guardian（下次启动时不再启动 im.elf）
touch /etc/guardian.off
echo "已创建 /etc/guardian.off"
echo "guardian 会在下次循环时退出（最多 5 秒）"

#!/bin/sh
# 回滚 initrd
set -e
BAK="/etc/INITRD.IMG.orig"
DST="/system/INITRD.IMG"
BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

if [ ! -f "$BAK" ]; then
    echo "错误: 没有备份 $BAK"
    exit 1
fi

BAKSZ=$(run wc -c < "$BAK")
echo "备份大小: $BAKSZ B"
if [ "$BAKSZ" -lt 2000000 ]; then
    echo "错误: 备份异常，中止"
    exit 1
fi

run mount -o remount,rw /system || mount -o remount,rw /system
run cp "$BAK" "$DST"
run sync

NEWSZ=$(run wc -c < "$DST")
echo "已回滚: $NEWSZ B"
echo "重启生效: reboot"

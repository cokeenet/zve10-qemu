#!/bin/sh
# 在相机上执行：部署 initrd 补丁
# 用法: sh deploy.sh safe|hook
#
# 只用相机上确认可用的 busybox applet: cat cp mv rm ls grep dd sync mount umount md5sum wc
# 不用: awk stat cut head tail

set -e
MODE="${1:-safe}"
SRC="/etc/initrd_${MODE}.img"
DST="/system/INITRD.IMG"
BAK="/etc/INITRD.IMG.orig"

BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

echo "====================================="
echo "  部署 initrd 补丁 ($MODE)"
echo "====================================="
echo

# 1. 源文件
if [ ! -f "$SRC" ]; then
    echo "错误: 缺少 $SRC"
    echo "  请先推送 initrd_${MODE}.img 到 /etc/"
    exit 1
fi
SZ=$(run wc -c < "$SRC")
echo "源文件: $SRC ($SZ B)"
if [ "$SZ" -lt 2000000 ]; then
    echo "错误: 源文件太小（应约 2183168 B）"
    exit 1
fi

# 2. 备份原始（只一次）
if [ ! -f "$BAK" ]; then
    echo "备份原始 initrd ..."
    run cp "$DST" "$BAK"
    echo "已备份到 $BAK"
else
    echo "已有备份: $BAK"
fi
BAKSZ=$(run wc -c < "$BAK")
echo "  备份大小: $BAKSZ B"
if [ "$BAKSZ" -lt 2000000 ]; then
    echo "错误: 备份异常"
    exit 1
fi

# 3. /system 可写
echo
echo "检查 /system ..."
MOUNTLINE=$(run cat /proc/mounts | run grep ' /system ')
echo "  $MOUNTLINE"
case "$MOUNTLINE" in
    *" rw,"*) echo "  可写" ;;
    *)
        echo "  只读，remount rw"
        run mount -o remount,rw /system || mount -o remount,rw /system
        ;;
esac

# 4. 写入
echo
echo "写入 $DST ..."
run cp "$SRC" "$DST"
run sync

# 5. 回读
NEWSZ=$(run wc -c < "$DST")
echo "写入完成: $NEWSZ B"
if [ "$NEWSZ" != "$SZ" ]; then
    echo "错误: 大小不匹配（源 $SZ，目标 $NEWSZ）"
    exit 1
fi

# 6. md5
echo
echo "校验 md5 ..."
A=$(run md5sum "$SRC")
A=${A%% *}
B=$(run md5sum "$DST")
B=${B%% *}
echo "  源:   $A"
echo "  目标: $B"
if [ "$A" != "$B" ]; then
    echo "错误: md5 不匹配"
    exit 1
fi

echo
echo "====================================="
echo "  部署完成 ($MODE)"
echo "====================================="
echo
echo "下一步: reboot"
echo "重启后检查: cat /log/newmenu.log"

#!/bin/sh
# 用 /setting/env.txt 注入 LD_PRELOAD
# 用法: sh deploy.sh
set -e
LIB_SRC="/etc/nmlib.so"
LIB_DST="/etc/libnewmenu.so"
ENV="/setting/env.txt"

BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

echo "====================================="
echo "  通过 /setting/env.txt 注入"
echo "====================================="
echo

# 1. 库就位
if [ ! -f "$LIB_SRC" ]; then
    echo "错误: 缺少 $LIB_SRC"
    echo "  请先推送 libnewmenu.so 到 /etc/nmlib.so"
    exit 1
fi
SZ=$(run wc -c < "$LIB_SRC")
echo "库: $LIB_SRC ($SZ B)"
if [ "$SZ" -lt 100000 ]; then
    echo "错误: 库太小"
    exit 1
fi

run cp "$LIB_SRC" "$LIB_DST"
run chmod 755 "$LIB_DST"
echo "已复制到 $LIB_DST"
run md5sum "$LIB_DST"

# 2. 写 env.txt
printf 'LD_PRELOAD=%s\n' "$LIB_DST" > "$ENV"
run sync
echo
echo "已写 $ENV:"
run cat "$ENV" | while read line; do echo "  $line"; done

# 3. 检查 /setting 可写
echo
echo "检查 /setting 挂载:"
run cat /proc/mounts | run grep ' /setting '

echo
echo "====================================="
echo "  完成，重启生效"
echo "====================================="
echo
echo "  reboot"
echo
echo "回滚:"
echo "  rm /setting/env.txt && reboot"

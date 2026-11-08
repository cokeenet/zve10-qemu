#!/bin/sh
# 真机部署：用 /etc/ld.so.preload 交付 libnewmenu.so
# 这个方案在 QEMU 里已验证有效，且不依赖任何环境变量
#
# 用法: sh deploy_ldsopreload.sh
set -e
BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

SRC="/etc/nmlib.so"
DST="/etc/libnewmenu.so"
PRELOAD="/etc/ld.so.preload"
BAK="/etc/ld.so.preload.orig"

echo "====================================="
echo "  /etc/ld.so.preload 部署"
echo "====================================="
echo

[ -f "$SRC" ] || { echo "错误: 缺少 $SRC"; exit 1; }
SZ=$(run wc -c < "$SRC")
echo "库: $SRC ($SZ B)"
MD=$(run md5sum "$SRC" 2>/dev/null)
echo "  $MD"

run cp "$SRC" "$DST"
run chmod 755 "$DST"
echo "已复制到 $DST"

# 备份原 ld.so.preload（只一次）
if [ ! -f "$BAK" ]; then
    if [ -f "$PRELOAD" ]; then
        run cp "$PRELOAD" "$BAK"
        echo "已备份原 $PRELOAD -> $BAK"
    else
        printf '' > "$BAK"
        echo "原 $PRELOAD 不存在，创建空备份"
    fi
fi

# 保留原有条目，追加我们的库（去重）
TMP="/tmp/.ldsopreload.new"
: > "$TMP"
if [ -f "$PRELOAD" ]; then
    while IFS= read -r __l; do
        [ -n "$__l" ] || continue
        [ "$__l" = "$DST" ] && continue
        printf '%s\n' "$__l" >> "$TMP"
    done < "$PRELOAD"
fi
printf '%s\n' "$DST" >> "$TMP"
run cp "$TMP" "$PRELOAD"
run rm -f "$TMP"
echo "已写入 $PRELOAD"

run sync

echo
echo "--- 当前 $PRELOAD ---"
run cat "$PRELOAD"
echo
echo "--- 文件校验 ---"
run ls -la "$DST"
run md5sum "$DST"
echo
echo "====================================="
echo "  完成，重启生效"
echo "  reboot"
echo "====================================="
echo
echo "回滚:"
echo "  cp $BAK $PRELOAD && reboot"

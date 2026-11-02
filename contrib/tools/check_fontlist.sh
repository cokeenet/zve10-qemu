#!/bin/bash
# 验证「运行时 usr 镜像里的 fontlist.dat」是否与解包源树一致（只读检查，不改任何文件）
#
# 用法:
#   ./check_fontlist.sh <源树 fontlist.dat> <usr ext2 镜像>
#
# 示例:
#   ./check_fontlist.sh \
#       ~/unpacked/.../nflasha15_unpacked/share/app/fontlist.dat \
#       ~/work/zv1f_usr.img
#
# 依赖: debugfs（e2fsprogs）
set -u

if [ $# -lt 2 ]; then
  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi

SRC="$1"
IMG="$2"

if [ ! -f "$SRC" ]; then echo "!! 源树文件不存在: $SRC"; exit 1; fi
if [ ! -f "$IMG" ]; then echo "!! 镜像不存在: $IMG"; exit 1; fi
command -v debugfs >/dev/null || { echo "!! 缺 debugfs（apt install e2fsprogs）"; exit 1; }

RUNTIME=$(mktemp /tmp/fontlist_runtime.XXXXXX.dat)
trap 'rm -f "$RUNTIME"' EXIT

echo "=== 源树 fontlist.dat ==="
ls -la "$SRC"
echo "--- 内容（cat -A 显示 TAB 为 ^I）---"
cat -A "$SRC"
echo "--- md5 ---"
md5sum "$SRC"

echo
echo "=== 从镜像里提取 fontlist.dat ==="
# nflasha15 挂载点是 /usr，所以镜像根 == /usr
debugfs -R "cat /share/app/fontlist.dat" "$IMG" 2>/dev/null > "$RUNTIME"
if [ -s "$RUNTIME" ]; then
  ls -la "$RUNTIME"
  echo "--- 内容（cat -A）---"
  cat -A "$RUNTIME"
  echo "--- md5 ---"
  md5sum "$RUNTIME"
else
  echo "!! 从镜像读取失败或文件为空"
  echo "--- 尝试 debugfs ls /share/app/ ---"
  debugfs -R "ls /share/app/" "$IMG" 2>/dev/null | head -20
  exit 1
fi

echo
echo "=== 对比 ==="
if cmp -s "$SRC" "$RUNTIME"; then
  echo "✅ 运行时文件与源树一致"
else
  echo "❌ 不一致！差异（左=源树 右=镜像）："
  diff <(cat -A "$SRC") <(cat -A "$RUNTIME") | head -20
  exit 1
fi

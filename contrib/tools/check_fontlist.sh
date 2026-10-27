#!/bin/bash
# 验证运行时镜像里的 fontlist.dat 是否与源树一致
SRC=/mnt/g/WorkBuddy/zve10/unpacked/zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/share/app/fontlist.dat
IMG=/mnt/g/WorkBuddy/zve10/work/a6300emu/firmware/zv1f_usr_mon_objset.img

echo "=== 源树 fontlist.dat ==="
if [ -f "$SRC" ]; then
  ls -la "$SRC"
  echo "--- 内容（cat -A 显示 TAB 为 ^I）---"
  cat -A "$SRC"
  echo "--- md5 ---"
  md5sum "$SRC"
else
  echo "!! 源树文件不存在"
fi

echo
echo "=== 从镜像里提取 fontlist.dat ==="
# 用 debugfs 从 ext2 镜像里读
debugfs -R "cat /share/app/fontlist.dat" "$IMG" 2>/dev/null > /tmp/fontlist_runtime.dat
if [ -s /tmp/fontlist_runtime.dat ]; then
  ls -la /tmp/fontlist_runtime.dat
  echo "--- 内容（cat -A）---"
  cat -A /tmp/fontlist_runtime.dat
  echo "--- md5 ---"
  md5sum /tmp/fontlist_runtime.dat
else
  echo "!! 从镜像读取失败或文件为空"
  echo "--- 尝试 debugfs ls ---"
  debugfs -R "ls /share/app/" "$IMG" 2>/dev/null | head -20
fi

echo
echo "=== 对比 ==="
if [ -f "$SRC" ] && [ -s /tmp/fontlist_runtime.dat ]; then
  if cmp -s "$SRC" /tmp/fontlist_runtime.dat; then
    echo "✅ 运行时文件与源树一致"
  else
    echo "❌ 不一致！差异："
    diff <(cat -A "$SRC") <(cat -A /tmp/fontlist_runtime.dat) | head -20
  fi
fi

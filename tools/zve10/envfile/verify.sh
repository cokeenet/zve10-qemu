#!/bin/sh
# 部署后验证：确认 env.txt 生效，库进 im.elf
BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

echo "======================================"
echo "  部署后验证"
echo "======================================"
echo

PID=$(run pidof im.elf 2>/dev/null)
if [ -z "$PID" ]; then
    echo "✗ im.elf 未运行 - 相机可能没启动完"
    exit 1
fi
echo "im.elf pid=$PID"
echo

echo "[1] 库是否在 im.elf 的内存映射里"
CNT=$(run cat /proc/$PID/maps 2>/dev/null | run grep -c "libnewmenu")
echo "  libnewmenu 映射数: $CNT"
if [ "$CNT" -gt 0 ]; then
    echo "  ✓✓✓ 库已加载进 im.elf！"
    run cat /proc/$PID/maps 2>/dev/null | run grep "libnewmenu" | while read l; do echo "    $l"; done
else
    echo "  ✗ 库未加载"
fi
echo

echo "[2] 环境变量"
if [ -r /proc/$PID/environ ]; then
    run tr '\0' '\n' < /proc/$PID/environ 2>/dev/null | run grep -E "LD_PRELOAD|LD_LIBRARY" | while read l; do echo "    $l"; done
else
    echo "  (无法读 environ)"
fi
echo

echo "[3] 新菜单日志"
if [ -f /log/newmenu.log ]; then
    echo "  行数: $(run wc -l < /log/newmenu.log)"
    echo "  关键行:"
    run grep -E "=== boot|LD_PRELOAD=|readable=|MATCH|object_base|views_base|caution anchor|hooked|installed|SAFE MODE" /log/newmenu.log 2>/dev/null | \
        run tail -25 | while read l; do echo "    $l"; done
else
    echo "  ✗ /log/newmenu.log 不存在 - 库没运行过"
fi
echo

echo "[4] 结论"
if [ "$CNT" -gt 0 ]; then
    if run grep -q "installed object=" /log/newmenu.log 2>/dev/null; then
        echo "  ✓✓✓ hook 已安装 - 去相机上看菜单！"
    else
        echo "  ⚠ 库加载了但 hook 未安装 - 打开一次菜单再看日志"
    fi
else
    echo "  ✗ 库未加载 - 检查 /setting/env.txt 和 dmode"
fi
echo
echo "======================================"

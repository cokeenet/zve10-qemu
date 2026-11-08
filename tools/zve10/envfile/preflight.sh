#!/bin/sh
# 部署前自检：在相机上跑，把所有前提条件一次性查清楚
# 用法: sh preflight.sh
BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

echo "======================================"
echo "  ZV-E10 env.txt 注入 - 部署前自检"
echo "======================================"
echo

echo "[1] 系统信息"
run cat /proc/version 2>/dev/null | run cut -c1-80
echo "  uptime: $(run cat /proc/uptime 2>/dev/null)"
echo

echo "[2] 关键分区挂载"
for m in / /etc /setting /system /usr /log; do
    LINE=$(run cat /proc/mounts 2>/dev/null | run grep " $m ")
    if [ -n "$LINE" ]; then
        echo "  ✓ $m"
        echo "      $LINE"
    else
        echo "  ✗ $m 未挂载"
    fi
done
echo

echo "[3] dmode 门禁（set_oreore_env 的前提）"
if [ -f /setting/mode/dmode ]; then
    echo "  ✓ /setting/mode/dmode 存在"
    echo "      内容: [$(run cat /setting/mode/dmode 2>/dev/null)]"
else
    echo "  ✗ /setting/mode/dmode 缺失 - set_oreore_env 会被跳过！"
fi
echo

echo "[4] env.txt 当前状态"
if [ -f /setting/env.txt ]; then
    echo "  已存在:"
    run cat /setting/env.txt | while read l; do echo "    $l"; done
else
    echo "  (不存在，等待写入)"
fi
echo

echo "[5] 库文件"
for p in /etc/nmlib.so /etc/libnewmenu.so /z.so; do
    if [ -f "$p" ]; then
        SZ=$(run wc -c < "$p")
        MD=$(run md5sum "$p" 2>/dev/null)
        echo "  ✓ $p ($SZ B)"
        echo "      $MD"
    fi
done
echo

echo "[6] 现有 preload 配置"
for p in /etc/ld.so.preload /setting/mode/preload; do
    if [ -f "$p" ]; then
        echo "  $p:"
        run cat "$p" | while read l; do echo "    $l"; done
    else
        echo "  $p 不存在"
    fi
done
echo

echo "[7] im.elf 当前状态（改动前基线）"
PID=$(run pidof im.elf 2>/dev/null)
if [ -n "$PID" ]; then
    echo "  pid=$PID"
    echo "  maps 里的关键库:"
    run cat /proc/$PID/maps 2>/dev/null | run grep -E "libObj|viewUnified2|CautionConfig|libnewmenu" | \
        while read l; do echo "    $l"; done
else
    echo "  im.elf 未运行"
fi
echo

echo "[8] 写入测试（/setting 可写吗）"
if run touch /setting/.wtest 2>/dev/null; then
    run rm -f /setting/.wtest
    echo "  ✓ /setting 可写"
else
    echo "  ✗ /setting 不可写"
fi
echo

echo "======================================"
echo "  自检完成"
echo "======================================"

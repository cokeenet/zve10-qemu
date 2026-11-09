#!/bin/sh
# 部署后验证。相机上线后跑这个，逐条检查关键指标。
# 用法: sh verify_after_deploy.sh <相机IP>
set -e
IP="${1:?用法: sh verify_after_deploy.sh <相机IP>}"
HERE=$(cd "$(dirname "$0")" && pwd)
CAM="$HERE/../../payload/agent/cam.py"
cam() { python3 "$CAM" --ip "$IP" "$@"; }

echo "=================================================="
echo "  部署后验证  $IP"
echo "=================================================="
echo

echo "[1] 前置事实（先记下真机实际值）"
cam run '
echo "libObj.so:"
md5sum /usr/lib/libObj.so 2>&1
wc -c < /usr/lib/libObj.so 2>&1
echo "viewUnified2.so:"
md5sum /usr/lib/viewUnified2.so 2>&1
echo "CautionConfig.so:"
md5sum /usr/lib/CautionConfig.so 2>&1
echo "im.elf:"
md5sum /usr/bin/im.elf 2>&1
' 2>&1 | sed 's/^/    /'
echo "    期望 libObj.so = 12cf982dd3eaded426b0b9c703a681cd"
echo "    期望 viewUnified2.so = edb07179859e2fb6..."
echo

echo "[2] 部署文件是否就位"
cam run '
echo "ld.so.preload:"
cat /etc/ld.so.preload 2>&1
echo "---"
echo "libnewmenu.so:"
ls -la /etc/libnewmenu.so 2>&1
md5sum /etc/libnewmenu.so 2>&1
' 2>&1 | sed 's/^/    /'
echo

echo "[3] im.elf 是否真的加载了库"
cam run '
for p in /proc/[0-9]*; do
    c=$(cat $p/comm 2>/dev/null)
    if [ "$c" = "im.elf" ]; then
        echo "pid=${p#/proc/} comm=$c"
        echo "  maps 里 libnewmenu 行数:"
        grep -c libnewmenu $p/maps 2>/dev/null
        echo "  maps 里 libnewmenu 首行:"
        grep libnewmenu $p/maps 2>/dev/null | head -1
    fi
done
' 2>&1 | sed 's/^/    /'
echo

echo "[4] 菜单日志（我们库的输出）"
cam run '
for f in /log/newmenu.log /var/log/newmenu.log /tmp/newmenu.log; do
    [ -f "$f" ] && { echo "=== $f ==="; tail -40 "$f"; }
done
echo "(若都无输出，说明库未加载或日志路径不同)"
' 2>&1 | sed 's/^/    /'
echo

echo "=================================================="
echo "  判读"
echo "=================================================="
cat <<'EOF'
  [1] libObj.so md5 与期望一致 → 原厂库，锚点偏移有效
      不一致 → 库版本不同，锚点需重新取

  [3] maps 里有 libnewmenu 行 → 库确实加载进 im.elf
      没有 → ld.so.preload 未生效，看 [2]

  [4] 期望看到：
        NM === boot pid=... exe=/usr/bin/im.elf ===
        NM LD_PRELOAD=<unset>
        NM /etc/ld.so.preload readable=1
        NM ui pid=... lock=1
        NM MATCH libObj
        NM object_base=0x...
        NM caution anchor OK
        NM hooked CautionConfig init
        NM hooked viewUnified2 (3)
        NM installed object=0x... views=0x... modules=1
        NM NE_TREE_ADDED {"old_count":5,"new_count":6,...}
        NM NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}

      "NM warn: string_id hook failed (anchor occupied?)" 在这里【正常】，
      因为真机没有 libuiobserve 抢位置，所以更可能是成功装上。
      如果出现 warn，说明那两个地址被别的组件占了，标题文字可能不对，
      但菜单行本身不受影响。

  [4] 没有 NE_TREE_ADDED → 看 caller 门禁：
        NE_VERSION {"node":0,...}  → ne_view_handle / 符号查找问题
        NE_ERROR phase=append_row  → ne_add_row 失败，看 status
        完全没有 NE_*              → ne_modules_installed 没设或 hook 没装

  之后：打开相机菜单 → Setup 5 → 看是否出现 "New Menu" 行
EOF

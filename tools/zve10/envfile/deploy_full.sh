#!/bin/sh
# 一键部署：上传库 -> 写 ld.so.preload -> 校验
#
# 用法:
#   sh deploy_full.sh <相机IP>          部署
#   sh deploy_full.sh <相机IP> --dry    只检查不写入
#
# 前置：相机 agent 在 7777 端口运行（/etc/ld.so.preload 里挂着 agent）
# 回滚：sh rollback_ldsopreload.sh <相机IP>
set -e

IP="${1:?用法: sh deploy_full.sh <相机IP> [--dry]}"
DRY=0
[ "${2:-}" = "--dry" ] && DRY=1

HERE=$(cd "$(dirname "$0")" && pwd)
CAM="$HERE/../../payload/agent/cam.py"
LIB="$HERE/../menu/libnewmenu.so"

[ -f "$CAM" ] || { echo "错误: 找不到 $CAM"; exit 1; }
[ -f "$LIB" ] || { echo "错误: 找不到 $LIB（先跑 sh ../menu/build.sh slim）"; exit 1; }

cam() { python3 "$CAM" --ip "$IP" "$@"; }

echo "=========================================="
echo "  部署到 $IP"
echo "=========================================="
echo

# --- 0. 连通性 ---
echo "[0] 连通性"
if ! cam ping 2>/dev/null | grep -qi "pong\|ok\|alive"; then
    echo "  ✗ agent 无响应；相机离线或 agent 未运行"
    exit 1
fi
echo "  ✓ agent 在线"

# --- 1. 前置检查 ---
echo
echo "[1] 前置检查"
cam run 'ls -la /etc/ld.so.preload 2>&1; echo "---"; cat /etc/ld.so.preload 2>&1; echo "---"; md5sum /etc/nmlib.so 2>&1' | sed 's/^/  /'

# --- 2. 上传库 ---
echo
echo "[2] 上传库 ($(stat -c %s "$LIB") B, md5 $(md5sum "$LIB" | cut -d' ' -f1))"
if [ "$DRY" = "1" ]; then
    echo "  (dry run，跳过)"
else
    cam push /etc/nmlib.so --file "$LIB" | sed 's/^/  /'
fi

# --- 3. 部署 ---
echo
echo "[3] 写 ld.so.preload"
if [ "$DRY" = "1" ]; then
    echo "  (dry run，跳过)"
else
    cam push /tmp/.deploy.sh --file "$HERE/deploy_ldsopreload.sh" | sed 's/^/  /'
    cam run 'sh /tmp/.deploy.sh 2>&1' | sed 's/^/  /'
fi

# --- 4. 校验 ---
echo
echo "[4] 校验"
cam run 'echo "ld.so.preload:"; cat /etc/ld.so.preload; echo; echo "库:"; ls -la /etc/libnewmenu.so 2>&1; md5sum /etc/libnewmenu.so 2>&1' | sed 's/^/  /'

echo
echo "=========================================="
echo "  完成"
echo "  重启生效: 在 agent 里跑 reboot"
echo "  回滚:     sh rollback_ldsopreload.sh $IP"
echo "=========================================="

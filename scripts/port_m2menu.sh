#!/bin/bash
# ============================================================================
# 移植 ZV-1M2 新菜单触摸 UI → ZV-E10 一代
#
# 依据：docs/PORT_MANIFEST.md（2026-11-05）
#   两代同为 ASTRA (CXD90045) 平台、内核 vermagic 3.0.27_nl-rt106+、glibc 2.11
#   ZVE10 一代 UI 在 viewUnified*.so；ZV-1M2 UI 在 appFw.so + appFwBoot.so
#
# 用法：./port_m2menu.sh <输出目录>
# ============================================================================
set -euo pipefail

OUT="${1:?用法: $0 <输出目录>}"
M2=/mnt/build/fw/COMPARE/extracted/ZV1M2
E10=/mnt/build/fw/COMPARE/extracted/ZVE10
E10M2=/mnt/build/fw/COMPARE/extracted/ZVE10M2

log() { printf '[port] %s\n' "$*"; }

[[ -e "$OUT" ]] && { echo "输出目录已存在: $OUT" >&2; exit 1; }
mkdir -p "$OUT"

# ---- 1. 先铺 ZVE10 一代的完整底子（保留原生功能）----
log "复制 ZVE10 一代基准树"
mkdir -p "$OUT"
for d in usr/lib usr/bin usr/kmod usr/share/app usr/codec usr/sabin usr/scenario \
         usr/wlan usr/wifi-scripts usr/local usr/tool usr/sbin lib etc; do
    [[ -d "$E10/$d" ]] || continue
    mkdir -p "$OUT/$(dirname "$d")"
    cp -a "$E10/$d" "$OUT/$d"
done
[[ -d "$E10/rootfs" ]] && cp -a "$E10/rootfs" "$OUT/" || true
[[ -d "$E10/system" ]] && cp -a "$E10/system" "$OUT/" || true
log "  基准树: $(find "$OUT" -type f | wc -l) 个文件"

# ---- 2. 用 ZV-1M2 的 kmod 覆盖（vermagic 100% 一致，102/103 同名）----
log "覆盖 usr/kmod 为 ZV-1M2 版"
rm -rf "$OUT/usr/kmod"; mkdir -p "$(dirname "$OUT/usr/kmod")"; cp -a "$M2/usr/kmod" "$OUT/usr/kmod"

# ---- 3. 放入 ZV-1M2 独有的 UI 栈 ----
log "放入 ZV-1M2 新菜单 UI 栈"
for f in appFw.so appFwBoot.so AdjChkConfig.so libuiobserve.so; do
    [[ -f "$M2/usr/lib/$f" ]] && cp -a "$M2/usr/lib/$f" "$OUT/usr/lib/" \
        && log "  + $f ($(stat -c%s "$M2/usr/lib/$f") B)"
done

# ---- 4. 补齐 ZV-1M2 独有依赖（30 个）----
log "补齐 ZV-1M2 独有依赖"
added=0
while IFS= read -r rel; do
    src="$M2/$rel"; dst="$OUT/$rel"
    [[ -f "$src" && ! -e "$dst" ]] || continue
    mkdir -p "$(dirname "$dst")"
    cp -a "$src" "$dst"; added=$((added+1))
done < <(cd "$M2" && find usr/lib usr/bin usr/lib/netconf -type f 2>/dev/null)
log "  新增 $added 个文件"

# ---- 5. 换 libIMDB.so（加载清单，决定加载 appFwBoot 而非 appFw）----
log "替换 libIMDB.so（清单入口 appFw.so → appFwBoot.so）"
cp -a "$M2/usr/lib/libIMDB.so" "$OUT/usr/lib/libIMDB.so"

# ---- 6. 菜单数据：ZV-1M2 的 .uxc（含 *_ASTRA.uxc）----
log "替换 usr/share/app 菜单数据"
rm -rf "$OUT/usr/share/app"; mkdir -p "$(dirname "$OUT/usr/share/app")"; cp -a "$M2/usr/share/app" "$OUT/usr/share/app"

# ---- 7. 校验 ----
log "校验"
fail=0
for f in appFw.so appFwBoot.so AdjChkConfig.so libIMDB.so; do
    if [[ -f "$OUT/usr/lib/$f" ]]; then
        printf '  ✅ %-20s %10s B\n' "$f" "$(stat -c%s "$OUT/usr/lib/$f")"
    else
        printf '  ❌ %-20s 缺失\n' "$f"; fail=1
    fi
done
# appFw.so 的 NEEDED 是否全部齐备
export PATH=/mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1/bin:$PATH
for lib in $(arm-linux-readelf -dW "$OUT/usr/lib/appFw.so" 2>/dev/null | grep NEEDED | sed 's/.*\[//;s/\]//'); do
    if find "$OUT" -name "$lib" 2>/dev/null | grep -q .; then :; else
        printf '  ⚠️  appFw.so 需要 %s —— 输出树里没有\n' "$lib"; fail=1
    fi
done
printf '  appFw.so NEEDED 检查完成\n'
[[ $fail -eq 0 ]] && log "✅ 移植树组装完成: $OUT" || log "⚠️ 有缺失，见上"

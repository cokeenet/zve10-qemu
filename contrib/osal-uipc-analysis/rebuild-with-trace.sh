#!/bin/bash
# rebuild_trace.sh — 装入 ioctl 探针库 → 重建镜像 → 跑一轮 → 抽 uipc_trace
#
# 关键顺序（不能乱）：
#   patch_zv1f_mon.sh 生成树 → 放探针库 → patch_bootfactor.py 改树的 appFw.so
#   → **重新 mke2fs 打包**（否则库和补丁都进不了 img）→ build_zv1f_gpe3 → run_dump
set -u
ROOT=/mnt/g/WorkBuddy/zve10
W=$ROOT/work
FW=$W/a6300emu/firmware
TREE=/tmp/zv1f_usr_mon_objset_tree
IMG=$FW/zv1f_usr_mon_objset.img

echo "########## 1. 编译 ioctl 探针库 ##########"
# FAKE7_COUNT=0 关闭伪造；=200 伪造前 200 次 0x80000007；=-1 无限
FAKE7_COUNT=${FAKE7_COUNT:-200}
echo "  FAKE7_COUNT=$FAKE7_COUNT"
arm-linux-gnueabi-gcc -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp \
  -fno-builtin -DFAKE7_COUNT="$FAKE7_COUNT" \
  -o "$W/libCoreSync_trace.so" "$W/libCoreSyncTrace.c" || exit 1
arm-linux-gnueabi-nm -D --defined-only "$W/libCoreSync_trace.so" | grep -E ' T ' | head -5
ls -l "$W/libCoreSync_trace.so"

echo "########## 2. 重建 /usr 树 ##########"
cd /tmp
BACKUPOSAL=1 BKNOABORT=1 USBNOINIT=1 STREAMPFN=1 FONTLISTABS=1 FONTSTASH=1 \
  bash "$W/patch_zv1f_mon.sh" objset 2>&1 | tail -4

echo "########## 3. 放入 ioctl 探针库 ##########"
if [ ! -f "$W/libCoreSync_trace.so" ]; then echo "!! 探针库缺失"; exit 1; fi
cp "$W/libCoreSync_trace.so" "$TREE/lib/"
ls -l "$TREE/lib/libCoreSync_trace.so"

echo "########## 4. boot_factor 补丁（改树里的 appFw.so）##########"
python3 "$W/patch_bootfactor.py" 2>&1 | tail -3

echo "########## 5. 重新打包 usr.img（把库与补丁一起打进去）##########"
rm -f "$IMG"
truncate -s 358109184 "$IMG"
mke2fs -q -F -t ext2 -b 1024 -I 128 -N 87720 -m 0 -O ^resize_inode -d "$TREE" "$IMG"
e2fsck -fy "$IMG" >/dev/null 2>&1
debugfs -R "dump /lib/libCoreSync_trace.so /tmp/vt.so" "$IMG" >/dev/null 2>&1
if cmp -s "$TREE/lib/libCoreSync_trace.so" /tmp/vt.so; then
  echo "  探针库回读校验 OK"
else
  echo "  !! 探针库回读不一致"; exit 1
fi
# 顺便确认 im.elf 是 diag_app.sh（含新 preload 逻辑）
debugfs -R "dump /bin/im.elf /tmp/vim.sh" "$IMG" >/dev/null 2>&1
if grep -q "libCoreSync_trace" /tmp/vim.sh 2>/dev/null; then
  echo "  im.elf 已含新 preload 逻辑 OK"
else
  echo "  !! im.elf 里没找到 libCoreSync_trace，diag_app.sh 可能没被拷进去"
fi

echo "########## 6. 造 eMMC 镜像 ##########"
USR_IMG="$IMG" KERNEL_IMG="$FW/zv1f_kernel_nc3.bin" \
  bash "$W/build_zv1f_gpe3.sh" --img-only 2>&1 | tail -4

echo "########## 7. 跑实验 ##########"
cd "$ROOT"
bash "$W/run_dump.sh" 200 240 2>&1 | tail -55

echo "########## 7. 抽 uipc_trace / fdtable ##########"
dd if=/tmp/zv1f_emmc_gpe3.img of=/tmp/set3.img bs=1M skip=28 count=20 status=none
for f in uipc_trace.txt fdtable.txt diag17.txt app.err; do
  if mtype -i /tmp/set3.img "::/$f" > "$W/d3_$f" 2>/dev/null; then
    echo "  d3_$f: $(stat -c%s "$W/d3_$f") B"
  fi
done
echo "########## 8. uipc_trace 摘要 ##########"
if [ -s "$W/d3_uipc_trace.txt" ]; then
  echo "  总行数: $(wc -l < "$W/d3_uipc_trace.txt")"
  echo "  ---- 前 30 行 ----"
  head -30 "$W/d3_uipc_trace.txt"
  echo "  ---- 后 20 行 ----"
  tail -20 "$W/d3_uipc_trace.txt"
  echo "  ---- REQ 无对应 RET 的请求（按顺序）----"
  python3 - "$W/d3_uipc_trace.txt" <<'PY'
import sys, collections
reqs = []
rets = collections.Counter()
for ln in open(sys.argv[1], errors='replace'):
    p = ln.split()
    if not p: continue
    if p[0] == 'REQ' and len(p) >= 4:
        reqs.append((p[2], p[3]))
    elif p[0] == 'RET' and len(p) >= 3:
        rets[p[2]] += 1
print("  REQ 总数=%d  RET 总数=%d" % (len(reqs), sum(rets.values())))
miss = collections.Counter()
for req, arg in reqs:
    if rets[req] > 0:
        rets[req] -= 1
    else:
        miss[req] += 1
print("  没有返回的请求码:")
for k, v in miss.most_common(10):
    print("     req=%s  x%d" % (k, v))
PY
else
  echo "  !! 没有 uipc_trace.txt（探针可能没加载，或 fd 判定没命中）"
fi
echo "DONE"

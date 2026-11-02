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
MODE=${PATCH_MODE:-best3}
TREE=/tmp/zv1f_usr_mon_${MODE}_tree
IMG=$FW/zv1f_usr_mon_${MODE}.img

echo "########## 1. 编译 ioctl 探针库 ##########"
# FAKE7_COUNT=0 关闭伪造；=200 伪造前 200 次 0x80000007；=-1 无限
FAKE7_COUNT=${FAKE7_COUNT:-200}
echo "  FAKE7_COUNT=$FAKE7_COUNT"
arm-linux-gnueabi-gcc -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp \
  -fno-builtin -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
  -DFAKE7_COUNT="$FAKE7_COUNT" \
  -o "$W/libCoreSync_trace.so" "$W/libCoreSyncTrace.c" || exit 1
arm-linux-gnueabi-nm -D --defined-only "$W/libCoreSync_trace.so" | grep -E ' T ' | head -5
ls -l "$W/libCoreSync_trace.so"

echo "########## 2. 重建 /usr 树 ##########"
cd /tmp
BACKUPOSAL=1 BKNOABORT=1 USBNOINIT=1 STREAMPFN=1 FONTLISTABS=1 FONTSTASH=1 \
  bash "$W/patch_zv1f_mon.sh" "$MODE" 2>&1 | tail -4

echo "########## 3. 放入 ioctl 探针库 ##########"
if [ ! -f "$W/libCoreSync_trace.so" ]; then echo "!! 探针库缺失"; exit 1; fi
cp "$W/libCoreSync_trace.so" "$TREE/lib/"
ls -l "$TREE/lib/libCoreSync_trace.so"

echo "########## 4. boot_factor 补丁（改树里的 appFw.so）##########"
python3 "$W/patch_bootfactor.py" "$TREE" 2>&1 | tail -3

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
bash "$W/run_dump.sh" ${OBS_SECS:-200} $(( ${OBS_SECS:-200} + 40 )) 2>&1 | tail -55

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
  echo "  ---- 按 seq 配对的统计（v2 格式）----"
  python3 - "$W/d3_uipc_trace.txt" <<'PY'
import sys, collections
# v2 格式：
#   REQ <seq> <tid> <t_us> <fd> <req> <arg> <qid> [<extra>]
#   RET <seq> <tid> <t_us> <fd> <req> <arg> <qid> [<extra>] <ret> <err>
reqs = {}; rets = {}
for ln in open(sys.argv[1], errors='replace'):
    p = ln.split()
    if not p: continue
    if p[0] in ('REQ','RET') and len(p) >= 8:
        seq = p[1]
        rec = {'tid':p[2], 't':p[3], 'fd':p[4], 'req':p[5], 'arg':p[6], 'qid':p[7]}
        if p[0]=='REQ':
            reqs[seq] = rec
        else:
            rec['ret'] = p[-2]; rec['err'] = p[-1]
            rets[seq] = rec
print("  REQ 总数=%d  RET 总数=%d  (按 seq 配对)" % (len(reqs), len(rets)))
print()
print("  按 cmd 统计：")
print("  %-12s %-6s %-6s %-6s" % ("cmd","REQ","RET","未返回"))
by = collections.defaultdict(lambda:[0,0,0])
for s,r in reqs.items():
    by[r['req']][0]+=1
    if s in rets: by[r['req']][1]+=1
    else: by[r['req']][2]+=1
for cmd in sorted(by):
    a,b,c = by[cmd]
    print("  %-12s %-6d %-6d %-6d" % (cmd,a,b,c))
print()
# cmd 7 的 qid 分布
print("  cmd 7 (rcv_msg_tmo) 的 qid 分布：")
c7 = collections.Counter()
for s,r in reqs.items():
    if r['req'].endswith('7') and r['qid'] != '-':
        c7[r['qid']] += 1
for q,n in c7.most_common(15):
    print("     %-12s x%d" % (q,n))
print()
# cmd 8 的 qid 分布
print("  cmd 8 (reg_msg_queue_cb) 的 qid 分布：")
c8 = collections.Counter()
for s,r in reqs.items():
    if r['req'].endswith('8') and r['qid'] != '-':
        c8[r['qid']] += 1
for q,n in c8.most_common(15):
    print("     %-12s x%d  %s" % (q,n, "★13A/139/313" if q in ('0x8f013a','0x8f0139','0x8f0313','0x8F013A','0x8F0139','0x8F0313') else ""))
print()
# 未返回的
miss = [(int(s),r) for s,r in reqs.items() if s not in rets]
miss.sort()
print("  ★ 未返回 %d 条（注意：这是【右删失】，不是『永不返回』）" % len(miss))
for s,r in miss[:20]:
    print("     seq=%-7s tid=%-5s cmd=%-12s qid=%-12s t=%s" % (s,r['tid'],r['req'],r['qid'],r['t']))
if len(miss)>20: print("     ... 共 %d 条" % len(miss))
print()
print("  ★ 线程分布（所有记录）：")
tids = collections.Counter(r['tid'] for r in reqs.values())
for t,n in tids.most_common(10):
    print("     tid=%-6s x%d" % (t,n))
PY
else
  echo "  !! 没有 uipc_trace.txt（探针可能没加载，或 fd 判定没命中）"
fi
echo "DONE"

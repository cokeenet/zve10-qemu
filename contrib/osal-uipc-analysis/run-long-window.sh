#!/bin/bash
# run_long.sh — 长窗口判定「线程饿死」vs「真卡死」
#
# 与 run_find.sh 的差别：
#   1. 观测窗口默认 600s（上游实测 export_ready 在 t=240s 才出现，200s 不够）
#   2. 运行期每 15s 采一次 QEMU 进程 CPU tick 增量（host 侧，零侵入）
#      → guest 在跑代码 ⇒ CPU% 高；全在 futex 睡 ⇒ CPU% 掉下来
#   3. 运行期每 30s 采一次 5 路串口日志大小（host 侧实时文件，可靠）
#   4. 300s 处额外截一张屏，看画面有没有变化
#
# 用法：bash run_long.sh [观测秒数=600] [定位基址最长等待秒数=240]
# 产出：
#   /tmp/h2_gdb.log    gdb 断点命中（带时间戳）
#   /tmp/h2_cpu.log    CPU tick 采样（第2列=累计 tick）
#   /tmp/h2_ser.log    串口日志大小采样
#   /tmp/h2_screen.ppm         结束时的画面
#   /tmp/h2_screen_mid.ppm     300s 时的画面
set -e
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-600}
MAXWAIT=${2:-240}
MID=$((SECS / 2))

echo "########## 1. 启动 QEMU ##########"
cd /tmp
rm -f /tmp/h2_gdb.log /tmp/h2_screen.ppm /tmp/h2_screen_mid.ppm \
      /tmp/hold_qmp /tmp/h2_mon /tmp/h2_cpu.log /tmp/h2_ser.log \
      /tmp/h2_u0.log /tmp/h2_u1.log /tmp/h2_u2.log /tmp/h2_u3.log /tmp/h2_u4.log
"$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/h2_u0.log -serial file:/tmp/h2_u1.log \
    -serial file:/tmp/h2_u2.log -serial file:/tmp/h2_u3.log \
    -serial file:/tmp/h2_u4.log \
    -gdb tcp::1234 \
    -qmp unix:/tmp/hold_qmp,server,nowait \
    -monitor unix:/tmp/h2_mon,server,nowait \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00100000" \
    > /tmp/h2_qemu.stderr 2>&1 &
QPID=$!
echo "  qemu pid=$QPID"
sleep 3

# ---- 采样器：CPU tick + 串口日志大小 ----
(
  while kill -0 $QPID 2>/dev/null; do
    t=$(awk '{print $14+$15}' /proc/$QPID/stat 2>/dev/null)
    echo "$(date +%H:%M:%S) ${t:-DEAD}" >> /tmp/h2_cpu.log
    s0=$(stat -c%s /tmp/h2_u0.log 2>/dev/null || echo 0)
    s1=$(stat -c%s /tmp/h2_u1.log 2>/dev/null || echo 0)
    echo "$(date +%H:%M:%S) u0=$s0 u1=$s1" >> /tmp/h2_ser.log
    sleep 15
  done
) &
SAMPLER=$!
echo "  sampler pid=$SAMPLER"

echo "########## 2. 盯 appFw 映射（最长 ${MAXWAIT}s）##########"
set +e
timeout $((MAXWAIT + 40)) python3 "$W/qmp_findelf.py" /tmp/hold_qmp "$MAXWAIT"
hold_rc=$?
set -e
if [ $hold_rc -ne 0 ]; then
  echo "  !! 没抓到 appFw 可读窗口"
  python3 - <<'PY'
import socket, time
try:
    s = socket.socket(socket.AF_UNIX); s.connect('/tmp/h2_mon')
    s.send(b'quit\n'); time.sleep(3); s.close()
except Exception:
    pass
PY
  kill $SAMPLER 2>/dev/null || true
  exit 1
fi

# ---- 300s 处截屏（后台定时）----
(
  sleep $MID
  python3 - "$MID" <<'PY'
import socket, sys, time
try:
    s = socket.socket(socket.AF_UNIX); s.connect('/tmp/h2_mon')
    s.send(b'screendump /tmp/h2_screen_mid.ppm\n'); time.sleep(4); s.close()
    print("  [mid] screendump @%ss done" % sys.argv[1])
except Exception as e:
    print("  [mid] screendump fail:", e)
PY
) &

echo "########## 3. gdb 接管：扫签名 → 下断点 → 观测 ${SECS}s ##########"
set +e
timeout "$SECS" stdbuf -oL -eL gdb-multiarch -nx -batch -x "$W/scan_hold.gdb" 2>&1 | \
  while IFS= read -r l; do echo "$(date +%H:%M:%S) $l"; done > /tmp/h2_gdb.log
set -e

echo "########## 4. 截屏 + 关机 ##########"
python3 - <<'PY'
import socket, time
def mon(cmd, wait=3):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect('/tmp/h2_mon')
        s.send(cmd.encode() + b'\n'); time.sleep(wait); s.close()
        return True
    except Exception as e:
        print('  monitor fail:', cmd.strip(), e); return False
print('  screendump:', mon('screendump /tmp/h2_screen.ppm', 3))
print('  quit      :', mon('quit', 3))
PY
wait $QPID 2>/dev/null || true
kill $SAMPLER 2>/dev/null || true
sleep 1

echo "########## 5. 结果：断点 ##########"
grep -a 'APPFW_BASE\|SIG_NOT\|hit at\|no sig\|fallback' /tmp/h2_gdb.log | head -10
echo "  --- HIT ---"
grep -a 'HIT' /tmp/h2_gdb.log | head -40 || true
echo "  (hits: $(grep -ac HIT /tmp/h2_gdb.log || true))"

echo "########## 6. 结果：CPU tick 曲线 ##########"
if [ -s /tmp/h2_cpu.log ]; then
  python3 - <<'PY'
rows=[]
for ln in open('/tmp/h2_cpu.log'):
    p=ln.split()
    if len(p)==2 and p[1].isdigit(): rows.append((p[0],int(p[1])))
if len(rows)>=2:
    print("  时间      tick增量  CPU%%(15s)")
    for i in range(1,len(rows)):
        d=rows[i][1]-rows[i-1][1]
        print("  %s  %6d   %5.1f%%" % (rows[i][0], d, d/15.0/100*100))
    print("  ---- 首末总增量 %d tick / %ds => 平均 %.1f%%" %
          (rows[-1][1]-rows[0][1], 15*len(rows), (rows[-1][1]-rows[0][1])/(15.0*len(rows))*100))
else:
    print("  !! cpu log 为空")
PY
else
  echo "  !! 无 /tmp/h2_cpu.log"
fi

echo "########## 7. 结果：串口日志增长 ##########"
if [ -s /tmp/h2_ser.log ]; then
  head -2 /tmp/h2_ser.log; echo "  ..."; tail -3 /tmp/h2_ser.log
else
  echo "  !! 无 /tmp/h2_ser.log"
fi

echo "########## 8. 截屏对比 ##########"
ls -la /tmp/h2_screen.ppm /tmp/h2_screen_mid.ppm 2>/dev/null || echo "  无截屏"

echo "########## 9. 抽 /setting ##########"
dd if="$OUT" of=/tmp/n2x.img bs=1M skip=28 count=20 status=none
for f in diag17.txt app.err app.out; do
  if mtype -i /tmp/n2x.img "::/$f" > "$W/h2_$f" 2>/dev/null; then
    echo "  h2_$f: $(stat -c%s "$W/h2_$f") B"
  fi
done
echo "########## 10. app.err 尾部 ##########"
tail -8 "$W/h2_app.err" 2>/dev/null || true
echo "DONE"

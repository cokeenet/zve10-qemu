#!/bin/bash
# run_plain.sh — 纯跑对照：不挂 gdb、不挂 QMP，只留 monitor 用于截屏/关机
#
# 目的：排除「gdb/QMP 观测本身打断了 App」这一干扰，回答
#   「没有观测干扰时，App 能不能自己走过 boot 流？」
# 判据：抽出的 diag17.txt 里 im.real 的 utime/stime 曲线是否持续增长
#
# 用法：bash run_plain.sh [纯跑秒数=320]
set -e
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-320}

echo "########## 1. 启动 QEMU（无 gdb / 无 qmp）##########"
cd /tmp
rm -f /tmp/p_screen.ppm /tmp/p_mon /tmp/p_cpu.log
"$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/p_u0.log -serial file:/tmp/p_u1.log \
    -serial file:/tmp/p_u2.log -serial file:/tmp/p_u3.log \
    -serial file:/tmp/p_u4.log \
    -monitor unix:/tmp/p_mon,server,nowait \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00100000" \
    > /tmp/p_qemu.stderr 2>&1 &
QPID=$!
echo "  qemu pid=$QPID"

# host 侧 CPU 采样
(
  while kill -0 $QPID 2>/dev/null; do
    t=$(awk '{print $14+$15}' /proc/$QPID/stat 2>/dev/null)
    echo "$(date +%H:%M:%S) ${t:-DEAD}" >> /tmp/p_cpu.log
    sleep 10
  done
) &
SAMPLER=$!

echo "########## 2. 纯跑 ${SECS}s（不 attach 任何调试器）##########"
sleep "$SECS"

echo "########## 3. 截屏 + 关机 ##########"
python3 - <<'PY'
import socket, time
def mon(cmd, wait=3):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect('/tmp/p_mon')
        s.send(cmd.encode() + b'\n'); time.sleep(wait); s.close()
        return True
    except Exception as e:
        print('  monitor fail:', cmd.strip(), e); return False
print('  screendump:', mon('screendump /tmp/p_screen.ppm', 3))
print('  quit      :', mon('quit', 3))
PY
wait $QPID 2>/dev/null || true
kill $SAMPLER 2>/dev/null || true
sleep 1

echo "########## 4. host CPU 曲线 ##########"
python3 - <<'PY'
rows=[]
try:
    for ln in open('/tmp/p_cpu.log'):
        p=ln.split()
        if len(p)==2 and p[1].isdigit(): rows.append((p[0],int(p[1])))
except FileNotFoundError:
    pass
if len(rows)>=2:
    print("  时间      CPU%%(10s)")
    for i in range(1,len(rows)):
        d=rows[i][1]-rows[i-1][1]
        print("  %s  %5.1f%%" % (rows[i][0], d/10.0))
else:
    print("  !! 无采样")
PY

echo "########## 5. 抽 /setting ##########"
dd if="$OUT" of=/tmp/pn2x.img bs=1M skip=28 count=20 status=none
for f in diag17.txt app.err app.out; do
  if mtype -i /tmp/pn2x.img "::/$f" > "$W/p_$f" 2>/dev/null; then
    echo "  p_$f: $(stat -c%s "$W/p_$f") B"
  fi
done
ls -la /tmp/p_screen.ppm 2>/dev/null || echo "  无截屏"
echo "DONE"

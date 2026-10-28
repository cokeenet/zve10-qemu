#!/bin/bash
# run_dump.sh — 在 boot_factor 断点命中时 dump 寄存器 + fp 链回溯
#
# 与 run_long.sh 的差别：
#   1. gdb 脚本用 scan_dump.gdb（→ bf_dump.gdb），命中时打印寄存器/栈/回溯
#   2. 窗口默认 200s（App 在 T+6s 就停，attach 后 4 秒内全部命中，不需要 600s）
#   3. 输出用 d2_ 前缀，不覆盖之前的 h2_*/p_* 结果
#   4. 不用 `set -e`：上一版的后台采样器被它掐死，只写了 2 行
#
# 用法：bash run_dump.sh [观测秒数=200] [定位基址最长等待秒数=240]
set -u
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-200}
MAXWAIT=${2:-240}

echo "########## 1. 启动 QEMU ##########"
cd /tmp
rm -f /tmp/d2_gdb.log /tmp/d2_screen.ppm /tmp/hold_qmp /tmp/d2_mon \
      /tmp/d2_cpu.log /tmp/d2_u0.log /tmp/d2_u1.log /tmp/d2_u2.log /tmp/d2_u3.log /tmp/d2_u4.log
"$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/d2_u0.log -serial file:/tmp/d2_u1.log \
    -serial file:/tmp/d2_u2.log -serial file:/tmp/d2_u3.log \
    -serial file:/tmp/d2_u4.log \
    -gdb tcp::1234 \
    -qmp unix:/tmp/hold_qmp,server,nowait \
    -monitor unix:/tmp/d2_mon,server,nowait \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00100000" \
    > /tmp/d2_qemu.stderr 2>&1 &
QPID=$!
echo "  qemu pid=$QPID"

# host 侧 CPU 采样（独立循环，脚本退出前不会被 set -e 掐掉）
(
  while kill -0 $QPID 2>/dev/null; do
    t=$(awk '{print $14+$15}' /proc/$QPID/stat 2>/dev/null)
    echo "$(date +%H:%M:%S) ${t:-DEAD}" >> /tmp/d2_cpu.log
    sleep 10
  done
) &
SAMPLER=$!

echo "########## 2. 盯 appFw 映射（最长 ${MAXWAIT}s）##########"
timeout $((MAXWAIT + 40)) python3 "$W/qmp_findelf.py" /tmp/hold_qmp "$MAXWAIT"
hold_rc=$?
if [ $hold_rc -ne 0 ]; then
  echo "  !! 没抓到 appFw 可读窗口"
  python3 - <<'PY'
import socket, time
try:
    s = socket.socket(socket.AF_UNIX); s.connect('/tmp/d2_mon')
    s.send(b'quit\n'); time.sleep(3); s.close()
except Exception:
    pass
PY
  kill $SAMPLER 2>/dev/null
  exit 1
fi

echo "########## 3. gdb 接管：扫签名 → 下断点 → 观测 ${SECS}s（含 dump）##########"
timeout "$SECS" stdbuf -oL -eL gdb-multiarch -nx -batch -x "$W/scan_dump.gdb" 2>&1 | \
  while IFS= read -r l; do echo "$(date +%H:%M:%S) $l"; done > /tmp/d2_gdb.log

echo "########## 4. 截屏 + 关机 ##########"
python3 - <<'PY'
import socket, time
def mon(cmd, wait=3):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect('/tmp/d2_mon')
        s.send(cmd.encode() + b'\n'); time.sleep(wait); s.close()
        return True
    except Exception as e:
        print('  monitor fail:', cmd.strip(), e); return False
print('  screendump:', mon('screendump /tmp/d2_screen.ppm', 3))
print('  quit      :', mon('quit', 3))
PY
wait $QPID 2>/dev/null
kill $SAMPLER 2>/dev/null
sleep 1

echo "########## 5. 抽取 dump 段 ##########"
grep -a -c "HIT" /tmp/d2_gdb.log 2>/dev/null | sed 's/^/  HIT 行数: /'
echo "  --- 命中概览 ---"
grep -a "^.......... HIT\|^.......... ===" /tmp/d2_gdb.log | head -20

echo "########## 6. 完整 dump（gbf 相关） ##########"
sed -n '/HIT gbf_entry/,/HIT gbf_SUCCESS/p' /tmp/d2_gdb.log | head -90
echo "  ....... gbf_SUCCESS 之后 ......."
sed -n '/HIT gbf_SUCCESS/,$p' /tmp/d2_gdb.log | head -90

echo "########## 7. 截屏 ##########"
ls -la /tmp/d2_screen.ppm 2>/dev/null || echo "  无截屏"

echo "########## 8. 抽 /setting ##########"
dd if="$OUT" of=/tmp/dn2x.img bs=1M skip=28 count=20 status=none
for f in diag17.txt app.err fdtable.txt; do
  if mtype -i /tmp/dn2x.img "::/$f" > "$W/d2_$f" 2>/dev/null; then
    echo "  d2_$f: $(stat -c%s "$W/d2_$f") B"
  fi
done
echo "DONE"

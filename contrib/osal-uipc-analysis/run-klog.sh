#!/bin/bash
# run_klog.sh — 跑一轮，在 App 冻结后从物理内存读出内核日志（klog）
#
# 依据：内核 cmdline 里有 `klog.addr=0x30000000 klog.size=0x00100000`
#       ⇒ printk 输出被重定向到物理 0x30000000 的 1 MB 环形区。
#       0x30000000 落在 DDR0(0x0..0x40000000) 内，QMP 的 pmemsave 可以直接读物理内存。
#
# 用法：bash run_klog.sh [等待秒数=150]
set -u
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-60}

echo "########## 1. 启动 QEMU（只开 qmp + monitor）##########"
cd /tmp
rm -f /tmp/k_qmp /tmp/k_mon /tmp/klog.bin /tmp/k_u0.log
"$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/k_u0.log -serial file:/tmp/k_u1.log \
    -serial file:/tmp/k_u2.log -serial file:/tmp/k_u3.log \
    -serial file:/tmp/k_u4.log \
    -qmp unix:/tmp/k_qmp,server,nowait \
    -monitor unix:/tmp/k_mon,server,nowait \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00800000" \
    > /tmp/k_qemu.stderr 2>&1 &
QPID=$!
echo "  qemu pid=$QPID"

echo "########## 2. 等 ${SECS}s 让 App 跑到冻结 ##########"
sleep "$SECS"

echo "########## 3. QMP pmemsave 读 klog ##########"
python3 - <<'PY'
import socket, json, time, sys

class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.s.settimeout(20)
        self.buf = b''
        self._recv()                       # greeting
        self.cmd('qmp_capabilities')
    def _recv(self):
        while b'\n' not in self.buf:
            d = self.s.recv(65536)
            if not d: raise RuntimeError('qmp closed')
            self.buf += d
        line, self.buf = self.buf.split(b'\n', 1)
        return json.loads(line)
    def cmd(self, name, **args):
        req = {'execute': name}
        if args: req['arguments'] = args
        self.s.send(json.dumps(req).encode() + b'\n')
        while True:
            r = self._recv()
            if 'return' in r or 'error' in r:
                return r

try:
    q = QMP('/tmp/k_qmp')
    # 先停一下，保证内存一致
    q.cmd('stop')
    # 读 1 MB：klog.addr=0x30000000, size=0x100000
    r = q.cmd('human-monitor-command',
              **{'command-line': 'pmemsave 0x30000000 0x800000 "/tmp/klog.bin"'})
    print('  pmemsave:', r)
    q.cmd('cont')
    q.cmd('quit')
except Exception as e:
    print('  QMP 失败:', e)
PY

sleep 2
wait $QPID 2>/dev/null

echo "########## 4. 结果 ##########"
ls -l /tmp/klog.bin 2>/dev/null || echo "  !! 没有 klog.bin"
if [ -s /tmp/klog.bin ]; then
  echo "  非零字节数: $(tr -d '\0' < /tmp/klog.bin | wc -c)"
  echo "  ---- 可读片段（前 80 行）----"
  strings -a -n 6 /tmp/klog.bin | head -80
fi
echo "DONE"

set pagination off
set confirm off
set architecture arm
# ★ 不要设 set remote memory-read-packet-size 0x10000！设成那么大后 QEMU 对整块请求直接失败，
#   异常被 rd() 吞成 None → 全区间"读不到" → 误判 SIG_NOT_FOUND（2026-10-28 实测踩过）。
#   用默认包大小即可。

# ============================================================
# scan_hold.gdb — 前置：run_hold.sh 已用 QMP 把 VM 停在「内存可读」的 im.real 上下文。
# attach 后 CPU 上下文不变，直接扫签名 → 算基址 → 下断点 → continue 观测。
# ============================================================

target remote :1234

python
import gdb, time

SIG = bytes.fromhex('2de9f04796b006af0446391d07f13c00')
OFF = 0x172A1AC

def rd(a, n):
    try:
        return bytes(gdb.selected_inferior().read_memory(a, n))
    except Exception:
        return None

def scan_range(lo, hi, chunk=0x1000, tag=''):
    a = lo
    ok = 0
    n = 0
    t0 = time.time()
    while a < hi:
        m = rd(a, chunk)
        n += 1
        if m:
            ok += 1
            i = m.find(SIG)
            if i >= 0:
                gdb.write('%s: hit at %#x (%d/%d blocks ok, %.1fs)\n'
                          % (tag, a + i, ok, n, time.time() - t0))
                return a + i
        a += chunk
    gdb.write('%s: no sig (%d/%d blocks readable, %.1fs)\n'
              % (tag, ok, n, time.time() - t0))
    return None

# 实测 6 次基址都落在 0x18AE0000~0x19130000（4KB 对齐），
# 签名绝对地址 = base + 0x172A1AC ⇒ 落点约 0x19C10000~0x1A640000。
hit = scan_range(0x19C00000, 0x1A800000, tag='hot')
if hit is None:
    gdb.write('hot range miss, fallback to full 0x18000000-0x1C000000\n')
    hit = scan_range(0x18000000, 0x1C000000, tag='full')

if hit is None:
    gdb.write('SIG_NOT_FOUND\n')
    gdb.execute('detach')
    gdb.execute('quit 1')

b = hit - OFF
gdb.execute('set $ab = %d' % b)
gdb.write('★★ APPFW_BASE = %#x  (sig at %#x)\n' % (b, hit))
end

printf "ATTACHED base=%#x\n", $ab

source /mnt/g/WorkBuddy/zve10/work/bf_dump.gdb

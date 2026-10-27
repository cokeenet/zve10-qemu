#!/bin/bash
# run_diag53: fallback TEXTRM_FONT_LIST 兜底文件验证轮
# 如果 abort 消失 => Backup_read(0x810000) 确实失败，fallback 是死路
# 如果 abort 依旧 => 失败点在 fopen 之后（解析循环或更深）
set -e
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
cd /tmp

echo "############ 阶段 1：造镜像 ############"
BACKUPOSAL=1 BKNOABORT=1 USBNOINIT=1 DISCRIM=1 STREAMPFN=1 BYPASSCNF=1 bash "$ROOT/work/patch_zv1f_mon.sh" objset

echo
echo "############ 阶段 2：造 eMMC + 跑 100s ############"
USR_IMG="$FW/zv1f_usr_mon_objset.img" \
KERNEL_IMG="$FW/zv1f_kernel_nc3.bin" \
bash "$ROOT/work/build_zv1f_gpe3.sh" 100

echo
echo "############ 阶段 3：判据 ############"
python3 - <<'PY'
from pathlib import Path
import re
d = Path('/tmp/KLOG.bin').read_bytes()
i = d.find(b'Linux version')
seg = d[i:] if i >= 0 else d
end = len(seg)
while end > 0 and seg[end-1] == 0: end -= 1
txt = seg[:end].decode('latin1', 'replace')
Path('/mnt/g/WorkBuddy/zve10/work/KLOG_diag53.txt').write_text(txt, encoding='latin1')
bpm = len(re.findall(r'Bad page map', txt))
exc = len(re.findall(r'Exception Syndrome', txt))
print('KLOG_diag53.txt: %d 行' % len(txt.splitlines()))
print('---- 判定表 ----')
print('  Bad page map   : %d' % bpm)
print('  Exception      : %d  ← 0=abort 消失, 1=abort 依旧' % exc)
# app.err
apperr = Path('/mnt/g/WorkBuddy/zve10/work/d53_app.err')
if apperr.exists():
    t = apperr.read_text(errors='replace')
    print('---- app.err ----')
    for l in t.splitlines()[:8]:
        print(' ', l)
# app.out
appout = Path('/mnt/g/WorkBuddy/zve10/work/d53_app.out')
if appout.exists():
    t = appout.read_text(errors='replace')
    for l in t.splitlines():
        if 'TEXTRM' in l or 'fontlist' in l or 'fallback' in l or 'FixedArea' in l or 'stella' in l:
            print('  [out]', l.strip()[:90])
PY

echo
echo "############ 阶段 4：抽产物 ############"
dd if=/tmp/zv1f_emmc_gpe3.img of=/tmp/n2.img bs=1M skip=28 count=20 status=none
for f in app.out app.err; do
  if mtype -i /tmp/n2.img "::/$f" > "$ROOT/work/d53_$f" 2>/dev/null; then
    echo "   saved d53_$f  ($(stat -c%s "$ROOT/work/d53_$f") B)"
  fi
done
echo "=== app.err ==="
cat "$ROOT/work/d53_app.err" 2>/dev/null | head -10
echo "=== fallback 行 ==="
grep -i "fallback\|TEXTRM\|fontlist" "$ROOT/work/d53_app.out" 2>/dev/null | head -10
echo "DONE"

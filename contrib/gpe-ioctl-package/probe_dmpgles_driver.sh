#!/bin/bash
U=/mnt/g/WorkBuddy/zve10/unpacked/zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked
K=/mnt/g/WorkBuddy/zve10/unpacked/zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha3_unpacked/vmlinux.bin
echo "== /usr/kmod 列表 =="
ls "$U/kmod" | tr '\n' ' '; echo
echo
echo "== 内核二进制里搜 dmpgles2 / sugilite / vip 字符串 =="
for s in dmpgles2 sugilite bionz_vip grm_gles; do
  n=$(grep -ac "$s" "$K" 2>/dev/null)
  echo "  $s : $n"
done
echo
echo "== 内核里 dmpgles2 附近的字符串 =="
python3 - "$K" <<'PY'
import sys
from pathlib import Path
d = Path(sys.argv[1]).read_bytes()
i = d.find(b'dmpgles2')
while i >= 0 and i < len(d):
    print("  偏移 %#x : %r" % (i, d[max(0,i-40):i+50]))
    i = d.find(b'dmpgles2', i+1)
    if i > 0 and i > 4000000: break
PY
echo
echo "== kmod 里哪个 .ko 提到 dmpgles2 =="
grep -l "dmpgles2" "$U"/kmod/*.ko 2>/dev/null

#!/bin/sh
# 把 ptinject.elf 推到相机并执行一次注入。
# 用法: sh run_inject.sh [lib路径] [--repeat N]
# 前置: agent 在 10.42.0.158:7777 可达（即相机已起 WiFi）
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB=${1:-/etc/libnewmenu.so}
shift 2>/dev/null || true
cd "$HERE"

python3 - "$LIB" "$@" <<'PY'
import importlib.util, sys
from pathlib import Path
sys.path.insert(0, '/root/fw/oss/tools/payload/agent')
spec = importlib.util.spec_from_file_location('cam', '/root/fw/oss/tools/payload/agent/cam.py')
cam = importlib.util.module_from_spec(spec); spec.loader.exec_module(cam)

lib = sys.argv[1]
args = sys.argv[2:]
elf = Path('ptinject.elf').read_bytes()
print("push ptinject.elf (%d B)" % len(elf))
with cam.conn('10.42.0.158', 7777, 120) as s:
    print("  push:", cam.cmd_push(s, '/log/ptinject.elf', elf))

cmd = "/bin/busybox chmod 755 /log/ptinject.elf\n/log/ptinject.elf %s %s\necho EXIT=$?\n" % (
    lib, ' '.join(args))
with cam.conn('10.42.0.158', 7777, 300) as s:
    b, h = cam.cmd_run(s, cmd)
print((b or b'').decode(errors='replace'))
PY

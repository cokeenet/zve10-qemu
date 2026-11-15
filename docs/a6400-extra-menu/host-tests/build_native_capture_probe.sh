#!/bin/sh
# Build only: never installs or arms the observer.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
mkdir -p "$OUT"
python3 - "$ROOT/host-tests/camera-mpr_monio.elf" "$OUT" <<'PY'
from pathlib import Path
import hashlib,sys
p,out=map(Path,sys.argv[1:]);d=p.read_bytes();h=2166136261
assert hashlib.sha256(d).hexdigest()=='300ed9c5a90dc26c3390b5fdd85c5a6e4d5f26e24c6022aa56832f83a47a96bf'
for b in d:h=((h^b)*16777619)&0xffffffff
(out/'capture_probe_build.h').write_text(
 '#define CAPTURE_MONITOR "/usr/bin/mpr_monio.elf"\n'
 '#define CAPTURE_LOCK "/tmp_bt/a6400-capture-v1.lock"\n'
 '#define CAPTURE_STOP_FILE "/tmp_bt/a6400-capture-v4.stop"\n'
 f'#define CAPTURE_MONITOR_BYTES {len(d)}u\n'
 f'#define CAPTURE_MONITOR_FNV 0x{h:08x}u\n')
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -marm -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -std=c11 -fPIE -I"$OUT" -c "$ROOT/camera/native_capture_probe.c" -o "$OUT/probe.o"
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie -Wl,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" "$OUT/probe.o" -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-capture-probe-v4.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-capture-probe-v4.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();v=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and v and all(tuple(map(int,x.split('.')))<=(2,11,2) for x in v),v
print('BUILD_ONLY: finite read-only capture observer; camera libc',sorted(v))
PY
sha256sum "$OUT/a6400-capture-probe-v4.elf"

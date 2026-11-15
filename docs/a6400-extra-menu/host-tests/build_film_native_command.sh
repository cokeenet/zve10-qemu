#!/bin/sh
# Build only: never connects to a camera, mounts scratch or issues a command.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
MONITOR=${3:-$ROOT/host-tests/author-film-install-20261114/camera-mwf_monio.elf}
mkdir -p "$OUT"
python3 - "$MONITOR" "$OUT" <<'PY'
from pathlib import Path
import hashlib,sys
monitor,out=map(Path,sys.argv[1:])
expected='f2ab9ba60dd0b3d327e64b10b8e6c5451d59e6026a1b6702578852088cfd2a84'
if not monitor.is_file():
    raise SystemExit('Pinned camera-mwf_monio.elf missing; supply exact original monitor locally')
data=monitor.read_bytes()
assert hashlib.sha256(data).hexdigest()==expected,'native monitor SHA-256 mismatch'
h=2166136261
for b in data:h=((h^b)*16777619)&0xffffffff
(out/'film_native_command_build.h').write_text(
 '#define FILM_NATIVE_MONITOR "/usr/bin/mwf_monio.elf"\n'
 '#define FILM_NATIVE_INPUT "/tmp_bt/a6400-film-job/input.jpg"\n'
 '#define FILM_NATIVE_OUTPUT "/tmp_bt/a6400-film-job/output.jpg"\n'
 f'#define FILM_NATIVE_MONITOR_BYTES {len(data)}u\n'
 f'#define FILM_NATIVE_MONITOR_FNV 0x{h:08x}u\n')
print('PINNED_MONITOR',monitor,expected)
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -marm -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -std=c11 -fPIE -I"$OUT" -c "$ROOT/camera/film_native_command.c" -o "$OUT/native-command.o"
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie -Wl,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" "$OUT/native-command.o" -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-native-command.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-film-native-command.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();v=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and v and all(tuple(map(int,x.split('.')))<=(2,11,2) for x in v),v
print('BUILD_ONLY: one bounded native command; camera libc',sorted(v))
PY
sha256sum "$OUT/a6400-film-native-command.elf"

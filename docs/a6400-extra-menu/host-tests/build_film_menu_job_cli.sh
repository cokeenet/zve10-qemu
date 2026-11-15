#!/bin/sh
# Build only: no install, camera connection, UI hook or worker execution.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
UI=${3:?exact reviewed UI build directory}
MODULE_PATH=${UI_MODULE_PATH:-/tmp_bt/a6400-film-ui-v3-apply.so}
PERSISTENT=${FILM_SETTINGS_PERSISTENT:-0}
case "$PERSISTENT" in
 0|1) ;;
 *) echo "FILM_SETTINGS_PERSISTENT must be 0 or 1" >&2; exit 2 ;;
esac
# Match an explicitly reviewed UI build. Persistent variants need a distinct
# OUT/module path and fresh artifact pins, never the earlier finite binary.
mkdir -p "$OUT"
python3 - "$UI" "$OUT" "$MODULE_PATH" "$PERSISTENT" <<'PY'
from pathlib import Path
import hashlib,json,re,sys
ui,out=map(Path,sys.argv[1:3])
module=sys.argv[3];persistent=int(sys.argv[4])
data=(ui/'a6400-film-ui-v3.so').read_bytes()
symbols=(ui/'symbols.txt').read_text()
matches=re.findall(r'^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+OBJECT\s+LOCAL\s+DEFAULT\s+\d+\s+ui$',symbols,re.M)
assert len(matches)==1 and int(matches[0][1])==1120,matches
rva=int(matches[0][0],16)
h=2166136261
for b in data:h=((h^b)*16777619)&0xffffffff
(out/'film_menu_job_build.h').write_text(
 '#define FILM_JOB_PROC "/proc/149"\n'
 f'#define FILM_JOB_MODULE {json.dumps(module)}\n'
 f'#define FILM_JOB_MODULE_BYTES {len(data)}u\n'
 f'#define FILM_JOB_MODULE_FNV 0x{h:08x}u\n'
 f'#define FILM_JOB_UI_RVA 0x{rva:x}u\n')
(out/'pinned-ui.json').write_text(json.dumps({
 'path':module,'bytes':len(data),'fnv':f'{h:08x}',
 'sha256':hashlib.sha256(data).hexdigest(),'ui_rva':f'0x{rva:x}',
 'ui_bytes':1120,'lifetime_ms':0 if persistent else 600000,
 'build_defines':{'FILM_SETTINGS_PERSISTENT':persistent},
 'maximum_heartbeat_age_ms':2000,
 'policy':'OFF or any noncurrent state never authorizes a new job'
},indent=2)+'\n')
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -marm -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
OBJECTS=
for source in film_menu_job_cli film_settings_source film_settings_snapshot film_settings_maps film_settings_gate film_menu; do
 SNAPSHOT_FLAGS=
 if [ "$source" = film_settings_snapshot ]; then
  SNAPSHOT_FLAGS="-DFILM_SETTINGS_PERSISTENT=$PERSISTENT"
 fi
 $CC $COMMON $SNAPSHOT_FLAGS -std=c11 -fPIE -I"$OUT" -c "$ROOT/camera/$source.c" -o "$OUT/$source.o"
 OBJECTS="$OBJECTS $OUT/$source.o"
done
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie \
 -Wl,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" $OBJECTS -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-menu-job.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-film-menu-job.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();versions=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and versions
assert all(tuple(map(int,v.split('.')))<=(2,11,2) for v in versions),versions
assert not re.search(r'\bUND\b.*\b(?:fork|execve|execl|system|pwrite|rename|unlink|write|fwrite)\b',s)
print('BUILD_ONLY: read-only live-menu job CLI; camera libc',sorted(versions))
PY
sha256sum "$OUT/a6400-film-menu-job.elf"

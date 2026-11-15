#!/bin/sh
# Build only. This never contacts a camera or executes the stop request.
# Use a new OUT and the same reviewed persistent UI build as the menu job CLI.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
UI=${3:?exact persistent UI build directory}
MODULE_PATH=${UI_MODULE_PATH:-/tmp_bt/a6400-film-ui-v3-persist.so}
mkdir -p "$OUT"
python3 - "$UI" "$OUT" "$MODULE_PATH" <<'PY'
from pathlib import Path
import hashlib,json,re,sys
ui,out=map(Path,sys.argv[1:3]);module=sys.argv[3]
data=(ui/'a6400-film-ui-v3.so').read_bytes();symbols=(ui/'symbols.txt').read_text()
states=set(re.findall(r'^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+OBJECT\s+LOCAL\s+DEFAULT\s+\d+\s+ui$',symbols,re.M))
controls=set(re.findall(r'^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+OBJECT\s+GLOBAL\s+DEFAULT\s+\d+\s+a6400_ui_stop_requested_v1$',symbols,re.M))
assert len(states)==1 and int(next(iter(states))[1])==1120,states
assert len(controls)==1 and int(next(iter(controls))[1])==4,controls
rva=int(next(iter(states))[0],16);control=int(next(iter(controls))[0],16)
assert rva<=0xffffffff-1120 and control<=0xffffffff-4 and control%4==0
assert not rva<=control<rva+1120,'stop word overlaps UI state'
h=2166136261
for b in data:h=((h^b)*16777619)&0xffffffff
paths={'FILM_UI_STOP_PROC':'/proc/149','FILM_UI_STOP_MODULE':module,
 'FILM_UI_STOP_MARKER':'/tmp_bt/a6400-film-capture.stop',
 'FILM_UI_STOP_CAPTURE_LOCK':'/tmp_bt/a6400-capture-v1.lock',
 'FILM_UI_STOP_INFLIGHT':'/tmp_bt/a6400-film-capture.inflight',
 'FILM_UI_STOP_JOB_LOCK':'/tmp_bt/a6400-film-job.lock'}
header=''.join(f'#define {key} {json.dumps(value)}\n' for key,value in paths.items())
header+=(f'#define FILM_UI_STOP_MODULE_BYTES {len(data)}u\n'
 f'#define FILM_UI_STOP_MODULE_FNV 0x{h:08x}u\n'
 f'#define FILM_UI_STOP_UI_RVA 0x{rva:x}u\n'
 f'#define FILM_UI_STOP_CONTROL_RVA 0x{control:x}u\n'
 '#define FILM_UI_STOP_WAIT_MS 2000u\n')
(out/'film_ui_stop_build.h').write_text(header)
(out/'pinned-ui-stop.json').write_text(json.dumps({
 'path':module,'bytes':len(data),'fnv':f'{h:08x}',
 'sha256':hashlib.sha256(data).hexdigest(),'ui_rva':f'0x{rva:x}','ui_bytes':1120,
 'control_symbol':'a6400_ui_stop_requested_v1','control_rva':f'0x{control:x}',
 'control_bytes':4,'control_states':{'idle':0,'request':1,'UI_thread_stopped':2},
 'wait_ms':2000,'build_defines':{'FILM_SETTINGS_PERSISTENT':1},
 'request_gate':{'marker':paths['FILM_UI_STOP_MARKER'],
  'absent':[paths[key] for key in ('FILM_UI_STOP_CAPTURE_LOCK','FILM_UI_STOP_INFLIGHT','FILM_UI_STOP_JOB_LOCK')]},
 'policy':'Outer coordinator stops dispatch/capture and waits for jobs first; one owned word only; no Sony calls, freeze, hook removal or dlclose'
},indent=2)+'\n')
print(f'OWNED_UI_STOP_BUILD ui_rva=0x{rva:x} control_rva=0x{control:x} wait_ms=2000')
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -marm -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
OBJECTS=
for source in film_ui_stop_cli film_settings_source film_settings_snapshot film_settings_maps; do
 $CC $COMMON -std=c11 -DFILM_SETTINGS_PERSISTENT=1 -fPIE -I"$OUT" -c "$ROOT/camera/$source.c" -o "$OUT/$source.o"
 OBJECTS="$OBJECTS $OUT/$source.o"
done
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie \
 -Wl,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" $OBJECTS -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-ui-stop.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-film-ui-stop.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();versions=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and versions
assert all(tuple(map(int,v.split('.')))<=(2,11,2) for v in versions),versions
assert not re.search(r'\bUND\b.*\b(?:dlopen|dlclose|ptrace|kill|fork|execve|execl|system|rename|unlink)\b',s)
print('BUILD_ONLY: exact owned-word UI stop; camera libc',sorted(versions))
PY
sha256sum "$OUT/a6400-film-ui-stop.elf"

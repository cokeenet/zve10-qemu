#!/bin/sh
# Build only: never installs, starts a monitor, or dispatches a camera job.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
SCRIPT=${3:?final pinned camera RAM job script}
SETTINGS=${4:?exact camera settings CLI ELF}
ACTIVE=${FILM_CAPTURE_ACTIVE_MS:-600000}
PERSISTENT=${FILM_CAPTURE_PERSISTENT:-0}
JOB_TIMEOUT=${FILM_CAPTURE_JOB_TIMEOUT_MS:-600000}
BASELINE_TIMEOUT=${FILM_CAPTURE_BASELINE_TIMEOUT_MS:-60000}
mkdir -p "$OUT"
python3 - "$ROOT/host-tests/camera-mpr_monio.elf" "$SCRIPT" "$SETTINGS" "$OUT" "$ACTIVE" "$PERSISTENT" "$JOB_TIMEOUT" "$BASELINE_TIMEOUT" <<'PY'
from pathlib import Path
import hashlib,json,re,sys
monitor,script,settings,out=map(Path,sys.argv[1:5])
active,persistent,job_timeout,baseline_timeout=sys.argv[5:]
assert re.fullmatch(r'[1-9][0-9]{0,7}',active) and 1000<=int(active)<=86400000,'active_ms range'
assert persistent in ('0','1'),'persistent must be 0 or 1'
assert re.fullmatch(r'[1-9][0-9]{0,7}',job_timeout) and 1000<=int(job_timeout)<=86400000,'job timeout range'
assert re.fullmatch(r'[1-9][0-9]{0,7}',baseline_timeout) and 1000<=int(baseline_timeout)<=86400000,'baseline timeout range'
data=monitor.read_bytes()
assert hashlib.sha256(data).hexdigest()=='300ed9c5a90dc26c3390b5fdd85c5a6e4d5f26e24c6022aa56832f83a47a96bf','MPR monitor pin'
def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h
(out/'capture_probe_build.h').write_text(
 '#define CAPTURE_MONITOR "/usr/bin/mpr_monio.elf"\n'
 '#define CAPTURE_LOCK "/tmp_bt/a6400-capture-v1.lock"\n'
 '#define CAPTURE_STOP_FILE "/tmp_bt/a6400-film-capture.stop"\n'
 f'#define CAPTURE_MONITOR_BYTES {len(data)}u\n'
 f'#define CAPTURE_MONITOR_FNV 0x{fnv(data):08x}u\n')
text=[];pins={}
for macro,path,runtime in (
    ('SCRIPT',script,'/tmp_bt/a6400-film-ram-job.sh'),
    ('SETTINGS',settings,'/tmp_bt/a6400-film-menu-job.elf')):
    data=path.read_bytes()
    if macro=='SCRIPT':
        assert data.startswith(b'#!/bin/sh\n') and not re.search(rb'__[A-Z_]+__',data),'final RAM script placeholders'
    else:assert data.startswith(b'\x7fELF'),'settings must be target ELF'
    text += [f'#define FILM_CAPTURE_{macro} "{runtime}"\n',
             f'#define FILM_CAPTURE_{macro}_BYTES {len(data)}u\n',
             f'#define FILM_CAPTURE_{macro}_FNV 0x{fnv(data):08x}u\n']
    pins[macro.lower()]={'source':str(path.resolve()),'runtime':runtime,'bytes':len(data),
                         'fnv':f'{fnv(data):08x}','sha256':hashlib.sha256(data).hexdigest()}
for macro,runtime in (
    ('MANIFEST','/tmp_bt/a6400-film-capture.manifest'),
    ('INFLIGHT','/tmp_bt/a6400-film-capture.inflight'),
    ('JOB_LOG','/tmp_bt/a6400-film-capture.job.log'),
    ('RUNTIME_LOG','/tmp_bt/a6400-film-capture.runtime.log'),
    ('JOB_LOCK','/tmp_bt/a6400-film-job.lock')):
    text += [f'#define FILM_CAPTURE_{macro} "{runtime}"\n']
text += [f'#define FILM_CAPTURE_ACTIVE_MS {active}u\n',
         f'#define FILM_CAPTURE_PERSISTENT {persistent}\n',
         f'#define FILM_CAPTURE_JOB_TIMEOUT_MS {job_timeout}u\n',
         f'#define FILM_CAPTURE_BASELINE_TIMEOUT_MS {baseline_timeout}u\n']
(out/'film_capture_daemon_build.h').write_text(''.join(text))
pins.update(active_ms=int(active),persistent=bool(int(persistent)),job_timeout_ms=int(job_timeout),
            baseline_timeout_ms=int(baseline_timeout),
            monitor_sha256='300ed9c5a90dc26c3390b5fdd85c5a6e4d5f26e24c6022aa56832f83a47a96bf',
            runtime='/tmp_bt/a6400-film-capture-daemon.elf',arguments=['watch'],
            runtime_log='/tmp_bt/a6400-film-capture.runtime.log',
            policy='stable metadata heuristic; exact live menu gate; serialized native jobs; uncertain stops dispatch')
(out/'pinned-inputs.json').write_text(json.dumps(pins,indent=2)+'\n')
print('PINNED_INPUTS',json.dumps(pins,sort_keys=True))
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -marm -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -std=c11 -fPIE -I"$OUT" -c "$ROOT/camera/film_capture_daemon.c" -o "$OUT/capture-daemon.o"
$CC $COMMON -std=c11 -fPIE -I"$ROOT/camera" -c "$ROOT/camera/film_runtime_log.c" -o "$OUT/runtime-log.o"
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie -Wl,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" "$OUT/capture-daemon.o" "$OUT/runtime-log.o" -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-capture-daemon.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-film-capture-daemon.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();versions=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and versions
assert all(tuple(map(int,v.split('.')))<=(2,11,2) for v in versions),versions
print('BUILD_ONLY: serialized menu-controlled capture daemon; camera libc',sorted(versions))
PY
python3 - "$OUT" "$ROOT/camera/film_capture_daemon.c" "$ROOT/camera/native_capture_probe.c" <<'PY'
from pathlib import Path
import hashlib,json,sys
out,source,scanner=map(Path,sys.argv[1:])
path=out/'pinned-inputs.json';pins=json.loads(path.read_text())
data=(out/'a6400-film-capture-daemon.elf').read_bytes()
pins.update(daemon_sha256=hashlib.sha256(data).hexdigest(),daemon_bytes=len(data),
            source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            scanner_source_sha256=hashlib.sha256(scanner.read_bytes()).hexdigest())
path.write_text(json.dumps(pins,indent=2)+'\n')
PY
sha256sum "$OUT/a6400-film-capture-daemon.elf"

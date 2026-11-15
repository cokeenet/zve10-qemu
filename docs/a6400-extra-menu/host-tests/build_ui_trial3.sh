#!/bin/sh
# Build only; explicit camera libc. Never use the cross-toolchain static glibc.
# For A6400_TRIAL_MS=0, use a different OUT directory and module path from all
# finite builds; regenerate module length/hash/RVA pins from the new sources.
# Build its matching snapshot reader with -DFILM_SETTINGS_PERSISTENT=1.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?pulled camera glibc directory}
MODULE_PATH=${UI_MODULE_PATH:-/tmp_bt/a6400-film-ui-v3.so}
DISPLAY_MS=${A6400_DISPLAY_MS:-5000}
case "$DISPLAY_MS" in
 ''|0*|*[!0-9]*) echo "A6400_DISPLAY_MS must be a decimal integer from 1 to 20000" >&2; exit 2 ;;
esac
if [ "${#DISPLAY_MS}" -gt 5 ] || [ "$DISPLAY_MS" -gt 20000 ]; then
 echo "A6400_DISPLAY_MS must not exceed 20000" >&2; exit 2
fi
if [ "${A6400_COMBO_DIAGNOSTIC:-0}" = 1 ]; then
 DEFAULT_TRIAL_MS=180000
else
 DEFAULT_TRIAL_MS=30000
fi
TRIAL_MS=${A6400_TRIAL_MS:-$DEFAULT_TRIAL_MS}
case "$TRIAL_MS" in
 0) ;;
 ''|0*|*[!0-9]*) echo "A6400_TRIAL_MS must be 0 (persistent) or a decimal integer from 30000 to 600000" >&2; exit 2 ;;
esac
if [ "$TRIAL_MS" != 0 ] && { [ "${#TRIAL_MS}" -gt 6 ] || [ "$TRIAL_MS" -lt 30000 ] || [ "$TRIAL_MS" -gt 600000 ]; }; then
 echo "A6400_TRIAL_MS must be 0 (persistent) or between 30000 and 600000" >&2; exit 2
fi
INSTANCE_SUFFIX=${A6400_UI_INSTANCE_SUFFIX:-}
case "$INSTANCE_SUFFIX" in
 *[!A-Z0-9_]*) echo "A6400_UI_INSTANCE_SUFFIX must contain only A-Z, 0-9 or underscore" >&2; exit 2 ;;
esac
[ "${#INSTANCE_SUFFIX}" -le 16 ] || { echo "A6400_UI_INSTANCE_SUFFIX must not exceed16 characters" >&2; exit 2; }
DIAGNOSTIC_FLAGS="-DA6400_UI_DISPLAY_MS=$DISPLAY_MS -DA6400_UI_TRIAL_MS=$TRIAL_MS -DA6400_UI_INSTANCE_SUFFIX=\"$INSTANCE_SUFFIX\""
if [ "${A6400_RENDER_DIAGNOSTIC:-0}" = 1 ]; then
 DIAGNOSTIC_FLAGS="$DIAGNOSTIC_FLAGS -DA6400_UI_RENDER_DIAGNOSTIC=1"
fi
if [ "${A6400_COMBO_DIAGNOSTIC:-0}" = 1 ]; then
 [ "${A6400_RENDER_DIAGNOSTIC:-0}" = 0 ] && [ "${A6400_TOUCH_CLOSE_DIAGNOSTIC:-0}" = 0 ] || { echo "combo diagnostic requires automatic render/close-only modes off" >&2; exit 2; }
 DIAGNOSTIC_FLAGS="$DIAGNOSTIC_FLAGS -DA6400_UI_COMBO_DIAGNOSTIC=1"
fi
case "${A6400_SETTINGS_DIAGNOSTIC:-0}" in
 0) ;;
 1)
 [ "${A6400_COMBO_DIAGNOSTIC:-0}" = 1 ] || { echo "settings diagnostic requires combo diagnostic" >&2; exit 2; }
 DIAGNOSTIC_FLAGS="$DIAGNOSTIC_FLAGS -DA6400_UI_SETTINGS_DIAGNOSTIC=1"
 ;;
 *) echo "A6400_SETTINGS_DIAGNOSTIC must be 0 or 1" >&2; exit 2 ;;
esac
if [ "${A6400_TOUCH_CLOSE_DIAGNOSTIC:-0}" = 1 ]; then
 [ "${A6400_RENDER_DIAGNOSTIC:-0}" = 1 ] || { echo "touch-close requires render diagnostic" >&2; exit 2; }
 DIAGNOSTIC_FLAGS="$DIAGNOSTIC_FLAGS -DA6400_UI_TOUCH_CLOSE_DIAGNOSTIC=1"
fi
case "${A6400_INPUT_DIAGNOSTIC:-0}" in
 0) ;;
 1|2) DIAGNOSTIC_FLAGS="$DIAGNOSTIC_FLAGS -DA6400_INPUT_DIAGNOSTIC=$A6400_INPUT_DIAGNOSTIC" ;;
 *) echo "A6400_INPUT_DIAGNOSTIC must be 0, 1 or 2" >&2; exit 2 ;;
esac
mkdir -p "$OUT"
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -Os -Wall -Wextra -Werror -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -mthumb -nostdlib -static -no-pie -fno-pie -ffreestanding -fno-builtin -fomit-frame-pointer \
 "-DA6400_UI_MODULE_PATH=\"$MODULE_PATH\"" \
 -Wl,-T,"$ROOT/camera/native_ui_payload.ld",-e,ui_release_loader,--build-id=none \
 "$ROOT/camera/native_ui_load_payload.c" "$ROOT/camera/native_ui_trampoline.S" \
 -o "$OUT/payload.elf"
arm-linux-gnueabihf-objcopy -O binary -j .text "$OUT/payload.elf" "$OUT/payload.bin"
for source in native_film_ui_trial3 film_native_view film_menu; do
 $CC $COMMON $DIAGNOSTIC_FLAGS -marm -fPIC -c "$ROOT/camera/$source.c" -o "$OUT/$source.o"
done
$CC -mfloat-abi=hard -nostdlib -shared -Wl,--no-undefined \
 "$OUT/native_film_ui_trial3.o" "$OUT/film_native_view.o" "$OUT/film_menu.o" \
 -Wl,--no-as-needed "$LIBS/libdl-2.11.2.so" "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-ui-v3.so"
arm-linux-gnueabihf-readelf -lWdV "$OUT/a6400-film-ui-v3.so" > "$OUT/elf-compatibility.txt"
arm-linux-gnueabihf-readelf -sW "$OUT/a6400-film-ui-v3.so" > "$OUT/symbols.txt"
python3 - "$OUT" <<'PY'
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);s=(p/'elf-compatibility.txt').read_text()
versions=set(re.findall(r'Name: GLIBC_([0-9.]+)',s))
assert versions and all(tuple(map(int,v.split('.'))) <= (2,11,2) for v in versions),versions
assert '[libc.so.6]' in s and '[libdl.so.2]' in s
symbols=(p/'symbols.txt').read_text()
assert not re.search(r'\bUND\b.*\b(fopen|fwrite|fread|rename|access|fprintf)\b',symbols)
stop=set(re.findall(r'^\s*\d+:\s+([0-9a-f]+)\s+4\s+OBJECT\s+GLOBAL\s+DEFAULT\s+\d+\s+a6400_ui_stop_requested_v1$',symbols,re.M))
assert len(stop)==1,stop
print('OWNED_STOP_RVA 0x'+next(iter(stop))+' uint32: 0=idle, 1=request, 2=stopped-on-UI-thread')
assert len((p/'payload.bin').read_bytes())<=2296
print('TRIAL3 BUILD ONLY: original camera libc versions',sorted(versions),'no UI diagnostic file-I/O imports')
PY
sha256sum "$OUT/payload.bin" "$OUT/a6400-film-ui-v3.so"

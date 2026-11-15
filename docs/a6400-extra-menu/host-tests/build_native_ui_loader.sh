#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?camera glibc directory}
mkdir -p "$OUT"
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -Os -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -mthumb -nostdlib -static -no-pie -fno-pie -ffreestanding -fno-builtin -fomit-frame-pointer \
 -fno-unwind-tables -fno-asynchronous-unwind-tables \
 -Wl,-T,"$ROOT/camera/native_ui_payload.ld",-e,ui_release_loader,--build-id=none \
 "$ROOT/camera/native_ui_load_payload.c" "$ROOT/camera/native_ui_trampoline.S" \
 -o "$OUT/payload.elf"
arm-linux-gnueabihf-objcopy -O binary -j .text "$OUT/payload.elf" "$OUT/payload.bin"
$CC $COMMON -marm -fPIC -c "$ROOT/camera/native_ui_probe.c" -o "$OUT/probe.o"
$CC -mfloat-abi=hard -nostdlib -shared "$OUT/probe.o" -Wl,--no-as-needed \
 "$LIBS/libdl-2.11.2.so" "$LIBS/libc-2.11.2.so" -o "$OUT/a6400-ui-native.so"
$CC $COMMON -marm -fPIE -c "$ROOT/camera/native_ui_load_guard.c" -o "$OUT/guard.o"
$CC $COMMON -marm -c "$ROOT/camera/native_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie -Wl,-e,_start,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" "$OUT/guard.o" -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" \
 -o "$OUT/a6400-ui-load-guard.elf"
sha256sum "$OUT/payload.bin" "$OUT/a6400-ui-native.so" "$OUT/a6400-ui-load-guard.elf"

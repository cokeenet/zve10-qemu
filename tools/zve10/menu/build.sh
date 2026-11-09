#!/bin/sh
# 两个构建目标：
#   sh build.sh slim    -> libnewmenu.so      真机用，只有 dlopen hook + 菜单插入
#   sh build.sh full    -> libnativeentry.so  QEMU 验证用，含 headless 渲染 + 串口协议
set -e
TC=/mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1
CC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc
TARGET="${1:-slim}"

case "$TARGET" in
  slim)
    echo "== slim: libnewmenu.so =="
    $CC -shared -fPIC -O2 -std=gnu11 -Wall -I. \
        -Wl,--version-script=version.map \
        -o libnewmenu.so mini_main.c menu_shell.c -ldl -lpthread -lrt
    ls -la libnewmenu.so
    ;;
  full)
    echo "== full: libnativeentry.so =="
    ( cd full && $CC -shared -fPIC -O2 -std=gnu11 -Wall -I. \
        -o ../libnativeentry.so observer-derived.c menu_shell.c -ldl -lpthread -lrt )
    ls -la libnativeentry.so
    ;;
  *)
    echo "用法: sh build.sh [slim|full]"; exit 1 ;;
esac

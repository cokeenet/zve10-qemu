#!/bin/sh
# 回滚：删掉 env.txt 即可
ENV="/setting/env.txt"
BB=/bin/busybox
run() { if [ -x "$BB" ]; then "$BB" "$@"; else "$@"; fi; }

if [ -f "$ENV" ]; then
    run rm -f "$ENV"
    echo "已删除 $ENV"
else
    echo "$ENV 不存在"
fi
run sync
echo "重启即可恢复原厂行为: reboot"

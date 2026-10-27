#!/bin/bash
# run_fb_dump: 跑 QEMU 后 dump 帧缓冲，转 PNG 看画面
set -e
ROOT=/mnt/g/WorkBuddy/zve10
FW=$ROOT/work/a6300emu/firmware
W=$ROOT/work
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm
OUT=/tmp/zv1f_emmc_gpe3.img
SECS=${1:-95}

echo "== 复用已有镜像 =="
[ -f "$OUT" ] && echo "  镜像存在: $(stat -c%s "$OUT") B" || { echo "  !! 镜像不存在，先跑 run_diag88.sh"; exit 1; }

echo "== 跑 QEMU ${SECS}s + dump 帧缓冲 =="
rm -f /tmp/fb_seg6.bin /tmp/fb_gpures.bin /tmp/fb_gpedst.bin /tmp/fb_all.bin
cd /tmp
(
  sleep "$SECS"
  # 帧缓冲候选地址：
  #   seg6   = WBI 段6 @ 0x20200000 (42MB, GPU/显示内存)
  #   gpedst = GPE blit 目标 0x08B54800
  #   gpusrc = GPE blit 源   0x080C4880
  echo "pmemsave 0x20200000 0x00200000 fb_seg6.bin"       # 段6 前 2MB
  sleep 1
  echo "pmemsave 0x08B54800 0x00100000 fb_gpedst.bin"     # GPE 目标 1MB
  sleep 1
  echo "pmemsave 0x080C4880 0x00100000 fb_gpures.bin"     # GPE 源 1MB
  sleep 1
  echo "pmemsave 0x23E00000 0x00200000 fb_alt.bin"        # WBI 段7 (备选)
  sleep 1
  echo "quit"
) | timeout $((SECS + 60)) "$QEMU" -M cxd90045 -display none \
    -serial file:/tmp/gfb_u0.log -serial file:/tmp/gfb_u1.log \
    -serial file:/tmp/gfb_u2.log -serial file:/tmp/gfb_u3.log \
    -serial file:/tmp/gfb_u4.log \
    -monitor stdio \
    -drive if=mtd,index=1,file="$OUT",format=raw \
    -kernel "$FW/zv1f_kernel_nc3.bin" -initrd "$FW/zv1f_initrd.img" \
    -append "console=ttyAM0,115200n8 mem=1G@0 mem=256M@0x80000000 root=/dev/ram0 rw klog.addr=0x30000000 klog.size=0x00100000" \
    > /tmp/gfb.stderr 2>&1 || true

echo "== 抽产物 =="
for f in fb_seg6.bin fb_gpedst.bin fb_gpures.bin fb_alt.bin; do
  if [ -f "/tmp/$f" ]; then
    sz=$(stat -c%s "/tmp/$f")
    nonzero=$(python3 -c "
d=open('/tmp/$f','rb').read()
nz=sum(1 for b in d if b)
print(f'{nz}/{len(d)} ({100*nz//max(len(d),1)}%)')
")
    echo "  $f: $sz B, 非零 $nonzero"
    cp "/tmp/$f" "$W/$f"
  else
    echo "  !! $f 不存在"
  fi
done

echo "== GPE 活动 =="
grep -a -i 'GPE\|GPER\|GPEW\|GPEIRQ\|cpyfb\|blit' /tmp/gfb.stderr | tail -15

echo "== 转 PNG =="
python3 "$W/fb2png.py"
echo "完成"

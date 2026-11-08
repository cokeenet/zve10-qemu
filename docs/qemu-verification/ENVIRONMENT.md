# QEMU 验证环境（2026-11-08 建立）

## 路径

```
QEMU 二进制   /mnt/build/m2qemu/qsrc/build/qemu-system-arm   (QEMU 5.1.0)
             机器类型 -M zve10  (= Sony BIONZ CXD90045)

内核源码      /mnt/build/ref-chocotan-zve10/work/linux-kernel
内核构建      /mnt/build/m2qemu/work/kbuild-ui2
工具链        /mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1
              （gcc 9.3.0，必须用这个，不能用 gcc 6.4）

E10 用户态    /mnt/build/m2qemu/work/initramfs-e10
M2  用户态    /mnt/build/m2qemu/work/initramfs-ui   （原状态，已备份到 /tmp/initramfs_m2_backup.tar）
参考树        /root/fw/ref_e10/  （E10 完整用户态，218 MB usr + 4.9 MB root）
              /mnt/build/ref-chocotan-zve10/work/usr/
```

## 启动命令

```bash
OUT=/tmp/qemu_xxx_$(date +%H%M%S); mkdir -p "$OUT"
systemd-run --unit=zve10-xxx --collect --no-block --property=MemoryMax=6G \
  --property=StandardError=file:$OUT/qemu.err \
  --property=StandardOutput=file:$OUT/qemu.out \
  /mnt/build/m2qemu/qsrc/build/qemu-system-arm \
  -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel /mnt/build/m2qemu/work/kbuild-ui2/arch/arm/boot/Image \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none \
  -serial file:$OUT/serial0.log -serial file:$OUT/serial1.log \
  -monitor none -no-reboot
```

**注意**：
- 必须用 `systemd-run`，普通后台任务会被 SIGTERM 杀掉
- 必须用**绝对路径**（systemd-run 的工作目录不是当前目录）
- `-smp 4` 比 `-smp 1` 稳定（`smp=1` 会卡在 rtc0 之后）

## 重建 Image（改 initramfs 后必须做）

```bash
TC9=/mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1
export PATH="$TC9/bin:$PATH"
export LD_LIBRARY_PATH="$TC9/lib:$TC9/arm-buildroot-linux-gnueabihf/lib"

cd /mnt/build/m2qemu/work/initramfs-e10
find . -print0 | cpio --null -o -H newc --owner=0:0 --quiet \
  > /mnt/build/m2qemu/stage-current/ui-e10.cpio
cp /mnt/build/m2qemu/stage-current/ui-e10.cpio /mnt/build/m2qemu/work/ui.cpio.m2

cd /mnt/build/m2qemu/work/kbuild-ui2
make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- -j8 Image
```

**`-initrd` 参数不可用**：初始化内存 266 MB 会和 `memrsv` 区域重叠，
内核报 `INITRD ... overlaps in-use memory region - disabling initrd`。
必须用 `CONFIG_INITRAMFS_SOURCE` 内嵌。

## 启动序列正常标志

```
INITSEQ resolve odl_init
INITSEQ resolve KikiLogGenerator_init
...（共 60-61 条）
```
`serial0.log` 约 70-77 KB。

## QEMU 的限制（已知，与 libnewmenu 无关）

1. **停在 GPE 阶段**
   ```
   FixedAreaTop  : 20098000
   Mon Buf Size  : 00008000
   !!! GPE HW: PSC dummywrite disabled.
   ```
   之后不再前进。GPU 未完整建模，`CautionConfig.so` 在这个阶段之后才加载。

2. **大量 dlopen 返回 (nil)**
   38 个库（libgpe.so / libfan.so / libNVM.so 等）dlopen 失败，
   因为它们需要真机的 `LD_LIBRARY_PATH` 或依赖未建模的设备。

3. **CPU1-3 failed to boot: -38**
   只有 CPU0 工作，不影响单线程逻辑。

## 已验证可用的东西

| 项 | 结论 |
|---|---|
| LD_PRELOAD 加载 131 KB 库 | ✓ |
| dlopen 覆盖生效 | ✓ 24 次 libObj.so |
| object_base 解析 | ✓ 0x9c1f000 |
| views_base 解析 | ✓ 0x351b3000 |
| fork watcher | ✓ |
| 不影响启动 | ✓ INITSEQ 完整 |

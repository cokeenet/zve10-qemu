# libnewmenu.so —— QEMU 验证结论（2026-11-08）

## 验证环境

```
QEMU:        /mnt/build/m2qemu/qsrc/build/qemu-system-arm -M zve10
内核:        /mnt/build/m2qemu/work/kbuild-ui2/arch/arm/boot/Image
             （gcc 9.3.0 编译，内嵌 initramfs）
用户态:      /mnt/build/m2qemu/work/initramfs-e10/
             （E10 原版：libObj 21 MB / CautionConfig 17 MB / viewUnified2 14 MB）
启动:        systemd-run --unit=zve10-xxx --collect --no-block
```

## 三个真实 bug（QEMU 实证）

### ① 缺少 dlopen 覆盖 —— 最致命

**现象**：真机上 `object_base` / `views_base` 永远是 0

**原因**：之前用 `dl_iterate_phdr` 轮询，但库是运行时 `dlopen` 进来的，
轮询拿不到加载那一刻。

**修复**：覆盖 `dlopen`，转发后用 `strstr(path, ...)` 匹配（与上游
`libnativeentry.so` 一致）。

**QEMU 实测**：捕获 24 次 `libObj.so`，`object_base=0x9c1f000` 正确。

### ② 锁设计错误

**现象**：库加载了但 hook 装不上

**原因**：
```c
if (nm_acquire_lock() != 0) return;   // init/ash 先抢到锁
```

`/etc/ld.so.preload` 会让**每个进程**都加载本库。`init`、`ash` 比
`im.elf` 早启动，抢走了锁，等 `im.elf` 加载时锁已占用 → 直接 return
→ `im.elf` 里没有 dlopen 覆盖。

**修复**：锁失败也继续装 hook，锁只用于避免重复 fork watcher。

**QEMU 实测**：`NM load pid=120 lock=1` 和 `NM load pid=130 lock=0`
两个实例都正确工作。

### ③ pthread_create 在构造函数阶段死锁

**现象**：serial 冻结在 `NM spawn poll thread`

**原因**：`ld.so` 构造函数阶段，libpthread 尚未初始化完，创建线程死锁。

**修复**：改用 `fork` + 读父进程 `/proc/PID/maps`
（内核实时维护，能看到主进程后续 mmap 的库）。

## 重要副产物：用户态版本必须匹配

| 用户态 | viewUnified2.so 加载次数 | views_base |
|---|---|---|
| ZV-1M2（libObj 3.9 MB） | 0 | 拿不到 |
| ZV-E10（libObj 21 MB） | 4 | 0x351b3000 |

**⇒ libnewmenu.so 的硬编码偏移是给 E10 的，必须配 E10 用户态。**

## QEMU 限制（与库无关）

QEMU 里 `im.elf` 停在 GPE 阶段：
```
FixedAreaTop  : 20098000
Mon Buf Size  : 00008000
!!! GPE HW: PSC dummywrite disabled.
（之后不再前进）
```

`CautionConfig.so` 在这个阶段之后才被 `libObj.so` 加载，
所以 QEMU 里永远走不到 CautionConfig hook。**真机上 im.elf 完整，不受限。**

## 部署

```bash
# 真机 /etc/ld.so.preload
echo /log/libnewmenu.so > /etc/ld.so.preload
```

日志：`/log/newmenu.log`

正常日志序列：
```
NM load pid=NNN lock=1
NM dlopen path=/usr/lib/libObj.so flags=0x101 -> 0x...
NM MATCH libObj
NM object_base=0x...
NM caution sym=0x... base=0x... delta=0xb8eb21 file=/usr/lib/CautionConfig.so
NM hooked CautionConfig init
NM installed object=0x... views=0x...
```

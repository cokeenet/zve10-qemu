# 内核来源核对：我们的 zImage ≠ 真机 vmlinux

> 回答 QEMU 侧的两个问题：nflasha3 的 vmlinux/initrd 能否直接喂 QEMU、
> 以及我们一直在用的内核是否与真机同构建。

## 一、★ 结论：不是同一个构建，vermagic 不匹配

| 项 | Sony 真机 | 我们构建的 |
|---|---|---|
| 版本串 | `3.0.27_nl-rt106+` | `3.0.27_nl-rt106-qemu` |
| 构建者 | `<BUILDER>@<BUILDHOST>`，gcc 4.5.1 (Sony CE Linux 8.2.7.6) | 本地交叉工具链 gcc 6.4 |
| 构建时间 | `Fri Jun 5 03:41:12 JST 2026` | 2026-10 期间 |
| 模块 vermagic | `3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7` | `3.0.27_nl-rt106-qemu SMP preempt mod_unload ARMv7` |

实测样本：
```
纯净 /usr 的 /kmod/dmm.ko  → vermagic=3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7
我们 fake_gles/fake_gles.ko → vermagic=3.0.27_nl-rt106-qemu SMP preempt mod_unload ARMv7
```

⇒ **vermagic 只差版本后缀**（`-rt106+` vs `-rt106-qemu`），其余
（`SMP preempt mod_unload ARMv7`）相同。
⇒ 这**精确解释**了 c408 的 `udm_udate` trap：纯净 `/usr` 的 kmod 是按
`-rt106+` 编的，跑在 `-rt106-qemu` 内核上，模块装载后被拒/行为异常。

### 关键：`CONFIG_MODVERSIONS` 是关的

两个模块**都没有 `__versions` 段**，内核 `.config` 也没有 `CONFIG_MODVERSIONS`
⇒ **vermagic 是唯一的装载门槛**，没有符号 CRC 校验。

**⇒ 务实解法：重建内核时对齐 localversion 即可**

```
CONFIG_LOCALVERSION="-rt106+"
CONFIG_LOCALVERSION_AUTO=n        # 防止 setlocalversion 再追加 "+"
```
这样 release 变成 `3.0.27_nl-rt106+`，vermagic 与 Sony 完全一致
⇒ **纯净 `/usr` 的 103 个原厂模块可以直接装载**，不必混用我们自编的模块。

> 注意 `+` 在 Linux 里通常是 `setlocalversion` 检测到 git 工作区不干净时自动追加的
> 后缀——Sony 那次构建的树是 dirty 的。手动写进 LOCALVERSION 是对齐它的正确做法。

### 但：内核侧结论必须重验

我们此前的内核侧工作（GPE 探针、streamfb/ramfb、`fake_gles.ko`、各类
trace 补丁）全部是在**自编内核**上得到的。既然不是同一构建，
**这些结论要在真机 `vmlinux.bin` 上重新验证**——不能在错误底座上继续。

## 二、nflasha3 的 vmlinux/initrd：可以直喂 QEMU

### 版本一致性 ✅

| 证据 | 值 |
|---|---|
| `nflasha3/VMLINUX.BIN` 内版本串 | `Linux version 3.0.27_nl-rt106+ … Fri Jun 5 03:41:12 JST 2026` |
| `nflasha3/INITRD.IMG` 的 `/version.txt` | `30110_06.2026060501` |
| `/usr` 文件时间戳 | Jun 4–5 2026 |

⇒ 同一固件构建（**2.03 / 2026-06-05**）。

### 加载地址（`nflasha1/boot/LOADADDR.TXT`，33 字节）

```
0x00108000,0x00700000,0x00103000
  ↑kernel     ↑initrd     ↑（第三个，疑为 ATAGS/tags）
```

与 `KEMCO.TXT` 的 `initrd=0x700000,8M` **完全吻合** ✅

### 镜像形态（重要，别混用）

| 分区 | 文件 | 大小 | 形态 |
|---|---|---:|---|
| **nflasha3**（零售） | `VMLINUX.BIN` | 4,117,760 | **未压缩 Image**（含可读 `Linux version`，首指令 `msr CPSR_c,#0xd3`） |
| **nflasha3**（零售） | `INITRD.IMG` | 2,183,168 | **ext2**（`/sbin/init` chroot 启动器 + `/rootfs`） |
| nflasha1（**updater**） | `boot/VMLINUX.BIN` | 1,701,156 | **压缩 zImage**（无可读版本串） |
| nflasha1（**updater**） | `boot/INITRD.IMG` | 4,943,872 | **cramfs**（625 个文件） |

⇒ **零售 = nflasha3 那一对；nflasha1/boot 是 updater 的，不要混用。**

### 直接喂 QEMU 的可行性

- `VMLINUX.BIN` **未压缩** ⇒ 可直接 `-kernel` 或 `-device loader` 到 `0x108000`
- 占用区间：`0x108000 + 0x3ED000 ≈ 0x4F2000` < `0x700000` ⇒ **与 initrd 不重叠** ✅
- `INITRD.IMG` 2.18 MB 落在 `initrd=0x700000,8M` 的 8 MB 窗口内 ✅
- 记得同时遵守 `mem=` / `memrsv=` 那串（尤其 `memrsv=680M@0x15800000:uc`）
  与 `wbimem=` 各段，否则 WBI/日志区会踩到内核

## 三、给一致性重建的清单（直接可用）

```
内核      nflasha3/VMLINUX.BIN   → 0x00108000（未压缩，可直接加载）
initrd    nflasha3/INITRD.IMG    → 0x00700000
/kmod     nflasha15/kmod/        → 原厂模块，vermagic 3.0.27_nl-rt106+
/system   nflasha3 原样
/setting  nflasha2 原样（含 KEMCO.TXT）
探针注入  /setting/mode/preload（+ dmode=3p），不替换 im.elf
内核重建  CONFIG_LOCALVERSION="-rt106+" + LOCALVERSION_AUTO=n
```

> ⚠️ 若仍要跑自编内核，就必须**整套都用我们自己的模块**（不能混原厂 kmod），
> 且要接受"与真机内核不同源"这一前提；内核侧结论只能在重建后重验。

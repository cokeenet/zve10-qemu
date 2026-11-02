# 外部 ZV-E10 逆向成果：qn06142/zv-e10-re 对照（2026-10-23）

> 来源：<https://github.com/qn06142/zv-e10-re>（Sony-PMCA-RE 的分支，服务模式
> 路线，固件 2.02/2.03，内核 3.0.27）。本文只记录**与本仓库工作相交的部分**，
> 并逐条标注「我们已交叉验证 / 与本机证据冲突」。

## 1. 直接可用、且被我们交叉验证的

| 他们的结论 | 我们的核对 |
|---|---|
| `/usr/share/app/color_cmn.uxc` = **312 字节**，调色板记录 8 字节步长（`u16 id 0x4000..0x4022` + `u16 attr 0x3a09` + RGBA），记录 `0x400c`（偏移 `0xbc`）控制实时取景三分线 | **完全吻合**：`debugfs stat /share/app/color_cmn.uxc` → inode 34795、**Size 312**、mode 0755、属主 57285:1000 |
| 三分线颜色 4 字节改色（灰 `33 33 33 80` → 品红 `ff 00 ff ff`）实机可见 | 他们**实机验证**过；对我们意味着：**菜单配色可用此法**，且是 4 字节级、可逆、不动二进制 |
| 内核 cmdline 字段（`wdt.timeout=500`、`klog.addr=0x035A5000`、`alog.addr=0x03524FC0`、`memrsv=680M@0x15800000:uc`、`pm.resume=0x20021054`） | 与我们 `real_cmdline.txt` **逐项一致**（说明他们的 dump 可信） |

## 2. 与本机证据冲突，需保留意见

> ### ✅ 本条已由对方原作者澄清（2026-11-02，issue #4）—— **我们的判断是对的**
>
> `qn06142` 在 [cokeenet/zve10-qemu#4](https://github.com/cokeenet/zve10-qemu/issues/4)
> 中确认：
> > 「在零售版 ZV-E10（固件 2.0x）上，`/dev/nflasha15` 确实是标准的、未压缩的
> > `ext2` 文件系统。`mount -o remount,rw /usr` 之后的写入**重启后确实会保留**。
> > `/usr/share/app` 不会在开机时被清空。」
> >
> > 「`BURIED_TREASURE.md` 里那段是早期逆向 `emmc.ko` 时写的，里面含有来自
> > **其它索尼相机平台**的遗留/休眠 LZP 字符串；后来真机实测确认写入保留。
> > **我忘了回去撤回那一段。**」
>
> ⇒ **「LZP 压缩分区」说法作废**，本节下方原先的对照记录保留作为历史，
> 但结论已明确：`nflasha15` = 普通 ext2，写入可持久化。
> 本仓库另外三篇文档曾错误采纳 LZP 说法，现已全部加上撤回声明。

**他们（早期文档）称 `/usr` 是 LZP 压缩块设备（`/dev/dnflasha15`），写入只落进页缓存、重启丢弃。**

我们的实测相反：

```
zve10_delivery/nflasha15.img
  ext2 magic = 0xEF53 ✓ 有效 ext2
  superblock state = 0x0001（干净，非脏）
  block_size = 1024, blocks = 243264
debugfs -R "ls -l /"  → bin/ codec/ kmod/ lib/ share/ … 全部可读
debugfs -R "ls /share/app" → viewMonitorBrightness.uxc … 文件齐全
```

⇒ 至少在我们这台（2.03）上，`nflasha15` 是**普通 ext2**，文件树就在分区里。
若他们的机器真走了 LZP，说明存在**压缩标志分支**；但他们「`/usr/share/app`
每次开机被重写」这一条，与『分区里就有这些文件』并不矛盾——可能是某个启动
步骤做了重新同步。**对移植方案的影响**：我们离线整区写 `nflasha15` 的路线
不受 LZP 说法影响（绕开文件系统层），但「刷完是否被回滚」需要真机验证一次。

**更正（2026-10-23 晚）**：上面这句结论**错了**，他们的 `wdt.mode=1 xrstreq=1`
才是对的。我方 `real_cmdline.txt` 是 **QEMU 实验改过的副本**（`console=ttyAM0`），
不是真机值。权威来源是 **`/setting/KEMCO.TXT`**（bootloader 读取）与真机
`/proc/cmdline`（`zve10_delivery/a_p0/01_cmdline.txt`，10-16 实抓），两者一致：

```
console=ttynull xrstreq=1 wdt.mode=1   ← 真机内核看门狗是【开启】的，500 ms
```

`nflasha1` 里 `@0x66C4C0` 的 `wdt.mode=0 / xrstreq=0` 只是会被覆盖的模板。
⇒ 与实测 `WDT TIMERCTL=0` 并不矛盾：**BootROM 串口启动路径会把它清掉**，
所以注入后读 0；而正常开机时它确实是武装的。

## 3. 对我们三条线的新增价值

### 3.1 定位 Darwin 的 XCS 引脚（直接服务当前主线）

他们在机内工具清单里提到 **`gpioctrl.elf`** 支持命名引脚，包括
**`GPIO_SB_0_XCS1` 这类 XCS 别名**：

```sh
gpioctrl dump            # dump 全部 GPIO 寄存器状态
gpioctrl read <PIN_NAME>
gpioctrl write <PIN> 0|1
```

⇒ 这就是我们缺的那块拼图：**用 `gpioctrl dump` 就能看出哪个 XCS 引脚被
SIO/Darwin 使用**，不必靠盲扫。前置条件是相机能进服务 shell（本机可以）。

### 3.2 调试新菜单启动失败（服务菜单移植主线）

- `klog.addr=0x035A5000`（内核 dmesg 环）、`alog.addr=0x03524FC0`
  （**应用日志 ~384 KB**）——刷完 hybrid `/usr` 若 UI 不启动，直接读这两块
  就能看到卡在哪（注意：这两个地址在 DRAM，**在我们的 `oem peek` 允许窗口
  之外**，需要扩窗口或用服务 shell 读）
- **串口 console**：改 `/setting/KEMCO.TXT` 把 `console=ttynull` 改成
  `console=ttyAM0,115200n8` ⇒ 内核日志走硬件 UART 测试点。配置持久化在
  `/setting`。这比 BootROM 注入更适合长时观察（不受 5~9 分钟预算限制）

### 3.3 无损注入路径（比我们当年踩雷的 preload 更"官方"）

Sony 出厂自带开发者钩子：

| 机制 | 用法 | 风险 |
|---|---|---|
| `/setting/mode/preload` + `dmode=3p` | `bootin.elf` 读该文件拼成 `LD_PRELOAD=` 注入 `im.elf`，**每次开机生效** | 不需要刷固件；但**坏 .so 同样会变砖**（正是我们踩过的坑） |
| `/setting/env.txt` + dmode 含 `e` | `set_oreore_env()` 逐行 `putenv`，所有服务继承 | 低 |
| `change_mode.sh 0/1/2` | 分别停在「内核后 / 全局 infra 后 / RTOS 后」 | 调试定位极好用 |
| `up.sh` + `ud_datcnv.elf` | 型号/区域/版本伪装，**强制更新器接受任意镜像** | 我们 L4 恢复路线可直接用 |

## 4. 能力边界（互补，不重叠）

他们有：服务模式 shell、UXC/调色板、IMDB/UIPC、更新器攻击面、UI 引擎定位
（`viewUnified2.so` 内含 `global.xdb` 字面量）。
他们**没有**：fastboot/BootROM（全文 0 命中）、裸机 payload、eMMC 原始读写、
Darwin 协议。⇒ 与本仓库的硬件通道正好互补：
**他们 = 系统能启动时的深度；我们 = 系统起不来时的唯一通道。**

## 5. 建议动作（按优先级）

1. 用 `gpioctrl dump` 定位 Darwin XCS 引脚 → 落地 `oem darwin`（当前主线）
2. 把 `console=ttyAM0` write 进 `KEMCO.TXT` 试串口日志（持久化，一次搞定）
3. 菜单移植验证阶段：优先读 `alog` 环（0x03524FC0）而不是盲猜
4. 需要再注入代码时，优先用 `/setting/mode/preload` + `dmode=3p`（官方钩子），
   而不是改 `/etc` 的 `ld.so.preload`——后者已经让我们变砖过一次

---

## 6. issue #4 澄清汇总（2026-11-02）

| 事项 | 对方澄清 | 我方原状态 | 处置 |
|---|---|---|---|
| `/usr` 是否 LZP 压缩 | **不是**，普通未压缩 ext2，remount rw 写入**持久** | 本仓库四篇文档采纳了 LZP 说法 | **全部加撤回声明**，`SAFETY_RULES.md` 规矩 2 保留但换了正确理由 |
| `BURIED_TREASURE.md` 矛盾原因 | 早期逆向 `emmc.ko` 残留的**其它平台** LZP 字符串，作者忘了撤回 | 我们把它当权威 | 已在各文档标注来源不可靠 |
| `wdt.mode=1 xrstreq=1` | 真机确实带 **500ms 看门狗**；BootROM 串口启动路径会清 `TIMER-IP` | 我们 2026-10-23 已自我更正 | ✅ 双方一致 |
| `0x20312000` 性质 | 是**描述符表**，不是 CPU 可见扫描输出像素 | 我们 `REALCAM_GROUND_TRUTH` §7 已同结论 | ✅ 双方一致 |
| 对方联系方式 | 邮箱 / Discord: Wheatley1901（在其 issue 评论区公开） | — | 仅记录，备用 |

**说明**：对方主动提出可以分发真机 dump，但**我们自己有真机与完整分区备份能力**，
不向他索取任何镜像或数据 —— 需要什么就自己 dump，不把本可自力完成的事变成别人的负担。

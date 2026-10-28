# zve10 — Sony ZV-E10（一代 / CXD90045 "ASTRA"）逆向与仿真研究

针对 **Sony ZV-E10（ILCE-ZV-E10，CXD90045 / "ASTRA" SoC，ARMv7 soft-float）**
的**互操作性研究**集合：真机救援与防砖、固件解密解包与系统测绘、
以及在 QEMU 里把原厂固件跑起来。

> **本仓库只包含原创的胶水与研究产出（补丁 / 脚本 / 文档 / 派生数据），
> 不含任何 Sony 固件、二进制、证书或其副本。** 使用者需自行从自己的设备导出固件。

> 🔥 **2026-10-22 突破：成果线 A 在真机闭环。** 硬件 fastboot 通道打通
> （BootROM 注入 → USB fastboot → eMMC 读+写），用它在真机执行了 1 字节
> 修复并找回 UI，并定位修复了 ~3 分 40 秒杀死会话的幸存看门狗。
> 见 [`docs/FASTBOOT_EMMC.md`](docs/FASTBOOT_EMMC.md)。
> 📌 **下次开工先读** [`docs/STATUS_20261023.md`](docs/STATUS_20261023.md)
> （当前状态、操作单、全部结论索引）。硬件实现代码在 [`fastboot/`](fastboot/)。


---

## 三条成果线

| 线 | 主题 | 代表产出 | 状态 |
|---|------|---------|------|
| **A** | **真机救援 / 防砖** | 硬件 fastboot/eMMC 通道、1 字节修复法、防砖兜底体系、启动桥硬件方案 | ✅ **已在真机完成**（注入/读/写/救砖/看门狗全链路） |
| **B** | **固件逆向 / 系统测绘** | 官方升级包解密解包、分区表、JIRITSU 通道、系统结构图 | ✅ 主要结构已测绘 |
| **C** | **QEMU 仿真** | 原厂固件完整启动 + 机身 App 运行 | 🟡 启动与 App 大部分打通，显示链路未完成（**2026-10-28：卡点定位到 uipc 应答缺失，见 [`contrib/osal-uipc-analysis/`](contrib/osal-uipc-analysis/)**） |

---

# A. 真机救援与防砖

### A1. 1 字节修复法（`/etc/ld.so.preload` 事故）

`/etc/ld.so.preload` 一旦指向一个**加载失败**的库，`/bin/busybox`（动态链接）
就起不来 ⇒ `/sbin/init` 起不来 ⇒ **开机循环、无数据模式**。

修复不需要重刷整块分区，**只需让该 preload 条目失效**：

| 方式 | 做法 | 需要什么 |
|------|------|---------|
| 软件（能拿到 shell 时） | `rm -f /etc/ld.so.preload` 或写入空白 | 相机还能执行命令 |
| **硬件（救砖）** ✅ **2026-10-22 已在真机执行** | BootROM 串口启动 → fastboot/stage2 → **清 FAT 目录项 size 字段（净变化 1 字节）** | Pico + 串口 + 电池 |

> ⚠️ **关键判据**：若那次写入让 preload 指向了一个**有未定义符号/重定位失败**的库，
> 则连 busybox 都无法启动，**任何软件层救援都不可能**（社区的 a7C II 工作也独立得出
> 同样结论）。此时只剩 BootROM 路线。

详见 [`docs/ANTIBRICK_SYSTEM.md`](docs/ANTIBRICK_SYSTEM.md)。

### A2. 分区写满的连锁失败

`/tmp`、`/etc`、`/var` 位于**同一块小容量 vfat 分区**。写满后**所有**写入都失败
（包括"清理 preload"的写入），因此救援写入必须**避免大文件**、优先 1~2 字节改动。

### A3. 启动桥硬件

参考接口与接线（USB TYPE-C / RP2040 桥）见
[`docs/GEN1_BOOT_HARDWARE_TYPEC.md`](docs/GEN1_BOOT_HARDWARE_TYPEC.md)。

### A4. 硬件 fastboot / eMMC 通道（✅ 2026-10-22 真机闭环）

BootROM 串口注入（Pico `zve10_uart_od 15` 协议）→ 裸机 fastboot payload
（USB `18d1:4ee0`）→ 运行时下发的 stage2 eMMC 驱动：

- **读+写均真机验证**：CMD17/24 扇区寻址、PIO；写路径经"死空间写图案→读回→
  还原"逐字节验证。stage2 默认只读（参数 `[2]=1` 才写），识别未达 TRAN
  态拒绝写
- **真机救砖**：BPB 驱动（无固定偏移）扫描定位 `ld.so.preload` 目录项，
  六道硬门校验下清 size 字段——净变化 1 字节，相机恢复 UI
- **看门狗发现**：App 经 `/proc/wdt` 武装的 TIMER-IP 看门狗跨 XRESET_REQ
  注入脉冲幸存，~3 分 40 秒准时复位 payload；payload 启动即执行内核自己的
  `WDT_SET_STOP` 修复。ILCE-7M4-RE / Powerful6700 均无此问题——本机是
  已知唯一案例
- **意义**：与磁盘系统完全独立的分区级救援通道，同时是 ZV-1F 新菜单
  移植（`nflasha15` 重建）的刷写路径

完整记录与关键技术事实（扇区寻址 vs 早期字节+DMA 假设、XRESET_REQ 不清
外设域、8 KB 注入预算、安全模型）见
[`docs/FASTBOOT_EMMC.md`](docs/FASTBOOT_EMMC.md)。实现与源码在工具库
zv-e10-toolkit（本地 `/root/fw/zv-e10-toolkit`，GitHub
`dino2048/zv-e10-toolkit`）。

---

# B. 固件逆向与系统测绘

| 文档 | 内容 |
|------|------|
| [`docs/FIRMWARE_REPORT.md`](docs/FIRMWARE_REPORT.md) | 从官方升级包到**完全解密解包**的完整流程 |
| [`docs/GEN1_SYSTEM_STRUCTURE.md`](docs/GEN1_SYSTEM_STRUCTURE.md) | 系统结构测绘（启动链、模块、服务、图形栈） |
| [`docs/JIRITSU_CHANNEL.md`](docs/JIRITSU_CHANNEL.md) | JIRITSU 命令通道的定位与协议 |
| [`docs/EXTERNAL_ZVE10_RE.md`](docs/EXTERNAL_ZVE10_RE.md) | 外部 ZV-E10 逆向成果对照（qn06142/zv-e10-re）：已验证项、冲突项、可用项 |
| [`docs/STATUS_20261023.md`](docs/STATUS_20261023.md) | ★ **收工存档 + 下次开工操作单**（先读这一页） |
| [`data/partition_map.tsv`](data/partition_map.tsv) | 分区表（含"表内偏移 → 镜像偏移 +0x400000"换算） |
| [`data/imdb_table.tsv`](data/imdb_table.tsv) | IMDB 表（App 会 dlopen 的库与符号） |
| [`data/machine_comparison.tsv`](data/machine_comparison.tsv) | A6400 / ZV-E10 / ZV-1F / ZV-E10 II 对照 |

### B1. 已测绘的图形/UI 栈（对"出画面"最关键的发现）

```
libObj.so (21 MB)  ── UI 主库
   ├─ ux::gfxeng        图形引擎：GraphicsEngine / Screen / Sprite / TextSprite
   ├─ andromeda         场景图：Window / Canvas / Scene3D / Node2D
   │                    ★ Render2DWalker / Render3DsWalker —— 纯软件遍历器
   ├─ stella            主题/颜色（CColor、TCompositeNode）
   └─ libra             资源（ImageData）
viewUnified2.so (14 MB) ── View 层 + ux::wgtsys 控件系统
GRM_*                  ── 图形驱动接口（Fill / Rectblit / BitmapWriteLockBits /
                          screenGetOnBitmap），定义在 libObj.so 内部
```

**关键判断：UI 可以纯软件渲染**（`Render2DWalker`/`OffScreenRender3D` 不是 GPU 命令流），
因此"出画面"未必需要建模 Mali。

---

# C. QEMU 仿真

把原厂固件搬进 QEMU，使其完整启动、加载平台模块并运行机身 App。

**当前进度（有日志证据）**

| # | 目标 | 状态 |
|---|------|------|
| 1 | 内核启动 → `/sbin/init` 挂载 chroot → **33 个平台模块**全部加载 | ✅ 完成 |
| 2 | 机身 App（`im.elf`）持续运行；`libObj.so`(UI 主库) 于 **16.85s** 加载 | ✅ 完成 |
| 3 | `liro`（4-MUSASHI 图像处理器固件）完整运行，派生 280 个内核线程 | ✅ 完成 |
| 4 | HDMI(`udm_hdmi`) / CEC(`udm_cec`) 驱动初始化成功 | ✅ 完成 |
| 5 | 无堆损坏、无 abort、无 SIGABRT（连续多轮零崩溃） | ✅ 完成 |
| 6 | **显示链路**：App → `/dev/dmpgles2` → `grm_gles.ko` → Mali | ❌ 未完成 |
| 7 | 输入链路（按键/触摸） | 🟡 模块已加载，未接到 UI |

**最佳记录**：guest 时间 **70.6s**，68~70 次模块加载，UIPC 处理器 40+ 种，零 page fault。

### C1. 复现

见 [`docs/HANDOFF_2026.md`](docs/HANDOFF_2026.md)（交接包）与
[`docs/HANDOFF.md`](docs/HANDOFF.md)。脚本：

```sh
./scripts/build_patched_emmc_safe.sh     # 打补丁并重建镜像
./scripts/run_qemu_zve10.sh              # 运行
./scripts/shell_zve10.sh "cmd"           # 客机 root shell（最强调试手段）
./scripts/shell_camera.sh "cmd"          # 挂载 + chroot 进相机 rootfs
```

### C2. 关键修复（摘要）

| 类别 | 内容 |
|------|------|
| QEMU 模型 | `hw/arm/bionz.c`：`boot_size=0x200000`、WBI 副段装载、DDR `1G+256M` |
| SD/eMMC | CID/CSD/EXT_CSD、CMD8/CMD6、**CMD23 块数掩码 `0xFFFF`** |
| 内核 | 地址换算兜底、`CONFIG_PCI`、**`tty-visible`（让 App 的用户态输出可见）** |
| 模块 | `backup.ko` 惰性初始化（新增 `R_ARM_ABS32` 重定位 + trampoline） |
| App 侧 | `/dev/tty`→`/dev/null` 符号链接、`__assert_fail`/`abort` 补丁、库符号链接补齐 |
| 环境 | **`/log`(nflasha11) 分区补齐为合法 vfat** ⇒ guest 时间 52.5s → 70.6s |

> 完整逐轮记录（含失败实验与排除假设）见
> [`docs/QEMU_BOOT_PROGRESS.md`](docs/QEMU_BOOT_PROGRESS.md)（约 1.4 万行）。

### C3. 显示链路：现状与路线

**实测结论（进程内探针，见 [`display/probe/`](display/probe/)）：**

```
探针注入                    5 个进程       ← 已可执行任意代码（LD_PRELOAD）
GRM_* 绘图接口调用次数        0             ← ★ 从未绘图
GraphicsEngine 初始化次数     0             ← ★ 图形引擎从未启动
屏幕/序列创建次数             0             ← ★ 从未创建 UI
viewUnified*.so 加载          0             ← ★ View 层从未加载
dlopen 失败项                0             ← 库加载全部成功
```

**⇒ 「没有画面」的根因不是缺少 GPU/Mali 模型，而是【UI 从未被创建】。**
App 的初始化按序加载基础设施库，停在网络栈（`libWlanResource`/`libWlanAdjust`）后
不再前进。给 `WlanAdjust::start` 打"返回成功"的半桩能再推进 6 个库，
但会崩在 `uipc` 共享区（桩未建立真实状态）。

三条可选路线：

| 路线 | 做法 | 评价 |
|------|------|------|
| **1（推荐）** | **强制创建屏幕**（照社区 a7C II 的思路）：在 preload 里直接调用屏幕/序列创建入口 | 只需"能调一个函数"，不必理解整个网络栈 |
| 2 | 完整模拟 WiFi/以太网服务 | 要理解 UIPC 服务协议，且仍可能卡在传感器/CP |
| 3 | 建模 Mali / SUGILITE | 研究级；且 `andromeda::Render2DWalker` 是纯软件遍历器，**未必需要** |

对本机型而言，路线 1 的落点是 `GRM_gperm*`（GPE 原语）与
`ux::gfxeng::Screen` / `AbstractSequenceFactory::CreateSequence`。

### C4. 路线图与工期预估（诚实估计）

| 阶段 | 内容 | 估计 |
|------|------|------|
| ① | 让 App 走到显示初始化（打开 `/dev/dmpgles2`） | **1~3 周** |
| ② | 软件 GPE/场景图 + 帧提交 + `ramfb` 出**静态画面** | **3~8 周** |
| ③ | 输入链路接上，画面可交互 | **1~2 周** |
| — | **合计（第一个"能动的画面"）** | **约 1.5~3 个月** |

**前置条件与风险**

- ① 的前提是拿到 App 的**内部日志**（内存环缓冲 `klog`/`alog`）——
  否则只能盲猜它为何在 ~33s 后静默。这是当前最高优先级。
- ② 的工作量取决于 GPE 语义与社区实现的相似度；若差异大，需按本机固件重写。
- **最大风险**：App 可能在等待实机才有的服务（CP/CMOS/镜头），
  若其 UI 初始化强依赖这些服务，则需先做服务级 stub，工期会显著延长。

---

## 目录结构

```
docs/                      研究文档（防砖、fastboot/eMMC 通道、固件报告、系统结构、QEMU 进展、交接…）
docs/ACKNOWLEDGEMENTS.md   ★ 参考项目与致谢
rescue/                    ★ 真机救援：payload 源码（裸机 ARMv7）+ 串口启动协议文档
  ├─ payloads/             探测 → 读块 → 定稿 → 最小修复（1~2 字节）的演进
  └─ docs/                 串口协议、方案定稿、现场行动手册
hardware/                  ★ 硬件研究：BootROM / UART 焊盘 / LED / Type-C / 探测排查
display/                   ★ 显示链路：GRM 接口清单 + 录制桩 `libgrm.c` + 路线取舍
tools/                     ★ 工具链
  ├─ port/                 移植可行性：ABI 闭包、反向闭包、IMDB/UXAC 解析、xrefs
  └─ abtest/               A/B 对照实验包装器
patches/kernel/            内核补丁 + 复用源码片段（含各类 trace）
patches/qemu/              QEMU 机器模型 / SD 卡补丁
scripts/                   构建脚本、引导脚本、桩库生成器、payload 运行器
data/                      派生技术数据（分区表、IMDB 表、机型对照）
guard/                     观测用 LD_PRELOAD 探针（夹紧/隔离/重定向/守卫）
```

---

## 调试方法论（本项目积累）

1. **让用户态输出可见**（`tty-visible` 内核补丁）—— 每个失败层次都会自己"打字"
2. **交互式客机 shell** —— 不必每次等几分钟看日志，可现场复现崩溃
3. **内核 printk 追踪** —— 比 gdb 可靠；`patches/kernel/*-trace.c` 是例子
4. **物理内存快照 + 离线分析** —— 堆损坏时去内存里找"被越界写进去的笔迹"
5. **A/B 对照** —— 每处改动都要能对比出真实收益

### 踩过的坑（避免重走）

- 没有内部约定信息时**不要做字节级猜测补丁**
- `debugfs` 原地替换文件不可靠，必须**回读校验**
- 大文件不要写 `/tmp`（tmpfs 满会静默失败）
- 别用 `pkill -f` 匹配自己（会杀掉自己的 shell）
- QEMU monitor 的 HMP 回显会打乱管道输入；需要脚本化请用 `-qmp`

---

## 法律与免责

- 本仓库**不包含**任何 Sony 固件、二进制、证书或派生副本
- 目的是**互操作性研究与个人设备改造**；请遵守当地法律与 Sony 软件许可
- 真机操作脚本会在你的相机上以 root 执行并修改可写分区，**风险自负**
- 商标与产品名归其各自所有者；本项目与 Sony 无关联

---

## 参与

欢迎 issue / PR。特别欢迎：

- **软件 GPE / 场景图软件渲染**相关经验
- **Mali / SUGILITE** 建模经验
- 其他 **CXD90045 / BIONZ 平台**设备的移植反馈

---

## 致谢

本工作建立在以下公开研究之上（详见 [`docs/ACKNOWLEDGEMENTS.md`](docs/ACKNOWLEDGEMENTS.md)）：

- **Lost-week-1025 / CXD90045-FPSU**
- **DavidBuchanan314** —— *ILCE-7M4-RE*、*Sony-ILCE-7M4-Linux*
- **ma1co** —— *Sony-PMCA-RE*、PMCA-QEMU、`fwtool.py`
- `chocotan/a7c2-re` —— a7C II 的原生 QEMU 相机实验（软件 GPE / ramfb 思路）
- `YuuLuo/a7c2-service-preload` —— 服务模式 / `ld.so.preload` 先行工作
- QEMU 社区，以及所有公开过相机逆向笔记的人

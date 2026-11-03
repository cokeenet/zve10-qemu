# ZV-E10 RE

[English](README.md) · **简体中文**

针对 **Sony ZV-E10**（ILCE-ZV-E10，CXD90045 / "ASTRA" SoC，ARMv7 soft-float）的**互操作性研究**：
真机救援与防砖、固件逆向、以及在 QEMU 里运行原厂固件。

[![Status](https://img.shields.io/badge/status-active-brightgreen)](#项目状态)
[![Platform](https://img.shields.io/badge/platform-ZV--E10%20%7C%20CXD90045-blue)](#)
[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-orange)](LICENSE)

> [!WARNING]
> **本仓库不包含任何 Sony 固件、二进制、证书或其副本。**
> 只发布原创的胶水代码、研究文档、补丁与派生技术数据。
> 固件需自行从自己的设备导出，合规责任自负。
> 详见 [NOTICE.md](NOTICE.md) 与[许可](#许可)。

---

## 目录

- [这是什么](#这是什么)
- [项目状态](#项目状态)
- [仓库结构](#仓库结构)
- [快速开始](#快速开始)
- [文档](#文档)
- [安全须知](#安全须知)
- [参与](#参与)
- [许可](#许可)
- [致谢](#致谢)

---

## 这是什么

同一台相机上的三条独立工作线：

| | 工作线 | 产出 | 状态 |
|---|---|---|---|
| **A** | **真机救援 / 防砖** | BootROM 串口注入、USB fastboot、eMMC 裸读写、1 字节修复 | **已在真机闭环** |
| **B** | **固件逆向 / 系统测绘** | 升级包解密解包、分区表、JIRITSU 通道、系统结构 | **主要结构已测绘** |
| **C** | **QEMU 仿真** | 原厂固件启动并运行机身 App | **能启动；显示链路未完成** |

三条线是互补而非串行的：系统起不来时 A 是唯一退路，B 解释系统由什么构成，C 让你不碰硬件也能迭代。

## 项目状态

以下为诚实快照，证据与日志见各链接文档。

### A — 真机救援 ✅

- **BootROM 串口注入**（Pico，`zve10_uart_od 15` 协议）→ 裸机 fastboot payload（USB `18d1:4ee0`）→ 运行时加载 stage2 eMMC 驱动。
- **读写均在真机验证。** 写路径通过"往死空间写图案 → 读回 → 还原"逐字节校验；stage2 默认只读，未达 TRAN 态拒绝写入。
- **真砖已修复。** 用 BPB 驱动（无固定偏移）扫描定位 `ld.so.preload` 目录项，清掉其 size 字段 —— 净变化**一个字节** —— 相机恢复 UI。
- **看门狗已摸清。** App 武装的 TIMER-IP 看门狗能跨过 XRESET_REQ 注入脉冲幸存，约 **3 分 40 秒**后复位 payload；payload 启动即调用内核自己的 `WDT_SET_STOP` 关闭它。在可对比的机型中，此现象目前只见于本机。

详见 [docs/FASTBOOT_EMMC.md](docs/FASTBOOT_EMMC.md) · [docs/FASTBOOT_OPERATIONS.md](docs/FASTBOOT_OPERATIONS.md) · [fastboot/](fastboot/)

### B — 固件逆向 / 系统测绘 ✅

完成官方升级包的完整解密解包、分区表重建、模块/服务清单与图形栈测绘。一个关键判断：UI 的场景图（`Render2DWalker`、`OffScreenRender3D`）是**纯软件遍历器**，因此"出画面"未必需要建模 Mali。

详见 [docs/FIRMWARE_REPORT.md](docs/FIRMWARE_REPORT.md) · [docs/GEN1_SYSTEM_STRUCTURE.md](docs/GEN1_SYSTEM_STRUCTURE.md) · [docs/JIRITSU_CHANNEL.md](docs/JIRITSU_CHANNEL.md)

### C — QEMU 仿真 🟡

原厂固件能启动、加载平台模块、并让机身 App（`im.elf`）持续运行，无堆损坏、无 abort。

| # | 里程碑 | 状态 |
|---|---|---|
| 1 | 内核启动 → `init` → **33 个平台模块**全部加载 | ✅ |
| 2 | 机身 App 运行；`libObj.so`（UI 主库）于 **16.85 s** 加载 | ✅ |
| 3 | `liro`（4-MUSASHI 图像处理器固件）运行，派生约 280 个内核线程 | ✅ |
| 4 | HDMI / CEC 驱动初始化 | ✅ |
| 5 | 多轮运行零堆损坏、零 abort、零 SIGABRT | ✅ |
| 6 | **显示链路**：App → `/dev/dmpgles2` → `grm_gles.ko` → GPU | ❌ |
| 7 | 输入链路（按键 / 触摸） | 🟡 模块已加载，未接到 UI |

当前卡点明确：图形驱动在 **IRQ 76** 上注册线程化中断，并在 `bar + 0x34` 读状态；我们只有一个最小模拟设备。它能送中断，驱动甚至也提交了少量栅栏包，但等完成位的那个 ioctl 永不返回。**这是我们这边的设备模型缺口。**

详见 [docs/QEMU_M2_PROGRESS.md](docs/QEMU_M2_PROGRESS.md) · [docs/analysis/qemu/QEMU_BOOT_PROGRESS.md](docs/analysis/qemu/QEMU_BOOT_PROGRESS.md)

## 仓库结构

```
fastboot/      BootROM 注入 + 两段式裸机 payload + 主机侧刷写/校验工具
  pico/          串口注入固件与主机侧注入器
  host/          bootstrap / flash / verify / 分区工具
  prebuilt/      stage1 与 stage2 预编译二进制
rescue/        裸机 payload 源码与串口启动协议
hardware/      BootROM、UART 焊盘、LED、Type-C、探测排查
display/       GRM 接口清单、录制桩、显示路线取舍
emulator/      QEMU 机器模型与菜单实验
modpkg/        固件模块包（最小集 / 替换集）
patches/       kernel/ 与 qemu/ 补丁及可复用 trace 片段
scripts/       构建、运行、客机 shell 辅助脚本
tools/         移植可行性：ABI 闭包、xrefs、IMDB/UXAC 解析
data/          派生数据表：分区表、IMDB 表、机型对照
guard/         观测用 LD_PRELOAD 探针
docs/          研究文档 —— 从 docs/README.md 开始
```

## 快速开始

以下假设你已自备固件 dump 与脚本所需工具链。

```sh
# 仿真
./scripts/build_patched_emmc_safe.sh     # 构建打过补丁的磁盘镜像
./scripts/run_qemu_zve10.sh              # 启动
./scripts/shell_zve10.sh "cmd"           # 客机 root shell（最好用的调试手段）
./scripts/shell_camera.sh "cmd"          # 挂载 + chroot 进相机 rootfs
```

硬件操作（**先读操作手册** —— 这些会动你的相机）：

```sh
# docs/FASTBOOT_OPERATIONS.md
cd fastboot && make build
```

## 文档

先看 **[文档地图](docs/README.md)**。多数人最先用得上的几篇：

| 文档 | 用途 |
|---|---|
| [docs/README.md](docs/README.md) | 300+ 篇文档的主题索引 |
| [docs/STATUS_20261023.md](docs/STATUS_20261023.md) | 状态快照 + 下次开工清单 |
| [docs/FASTBOOT_OPERATIONS.md](docs/FASTBOOT_OPERATIONS.md) | Pico 注入、接线、stage2、读校验刷写的完整流程 |
| [docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md](docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md) | 变砖复盘与恢复手册 |
| [docs/SAFETY_RULES.md](docs/SAFETY_RULES.md) | 写相机分区前的操作规矩 |
| [docs/ANTIBRICK_SYSTEM.md](docs/ANTIBRICK_SYSTEM.md) | 防砖设计与恢复边界 |
| [docs/GEN1_BOOT_HARDWARE_TYPEC.md](docs/GEN1_BOOT_HARDWARE_TYPEC.md) | 启动桥硬件 |
| [docs/EXTERNAL_ZVE10_RE.md](docs/EXTERNAL_ZVE10_RE.md) | 外部 ZV-E10 逆向成果对照 |
| [docs/ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md) | 本工作所依赖的前人成果 |

## 安全须知

> [!CAUTION]
> 硬件脚本以 **root** 身份在你的相机上运行并写分区。**确有把设备变成砖的可能。**

- **绝对不要**让任何 preload 机制指向一个加载失败的库 —— 连 shell 都会起不来，软件层救援随即失效。
- 写入是**持久**的，不会重启自动回滚：**写坏了会被记住**。动手前先做并校验完整备份。
- 第一次写之前先读 [docs/SAFETY_RULES.md](docs/SAFETY_RULES.md)。

## 参与

欢迎 issue / PR。特别欢迎：

- **软件光栅化 / 场景图渲染**相关经验（UI 路径可能完全是 CPU 绘制）
- QEMU 下的 **Mali / SUGILITE** 设备建模
- 其他 **CXD90045 / BIONZ** 机型的移植反馈

提 QEMU 相关 issue 时，请附串口日志与所构建的确切 commit。

## 许可

[PolyForm Noncommercial License 1.0.0](LICENSE)，仅限非商业用途；本仓库分发与否的边界见 [NOTICE.md](NOTICE.md)。

商标与产品名归其各自所有者。本项目与 Sony 无关联。

## 致谢

本工作建立在大量公开研究之上，完整清单见 [docs/ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md)。

- **Lost-week-1025** — CXD90045-FPSU
- **DavidBuchanan314** — ILCE-7M4-RE、Sony-ILCE-7M4-Linux
- **ma1co** — Sony-PMCA-RE、PMCA-QEMU、`fwtool`（上游维护者；原始版本出自 nex-hack）
- **chocotan** — a7c2-re（逆向研究）与 a7c2-qemu（QEMU 机器模型）
- **YuuLuo** — a7c2-service-preload（服务模式 / `ld.so.preload` 先行工作）
- **qn06142** — zv-e10-re（固件 2.0x 持久化与启动参数结论，并主动纠正我们的错误结论）
- **dino2048** — zv-e10-toolkit
- **ChambersXDU** — sony-a7c2-research
- **ice21415** — a6000-firmware-platform
- **BI2QFA** — a6300_updater_emu
- QEMU 社区，以及所有公开过相机逆向笔记的人

---

交流群（主要是中文，也欢迎英文）：**https://discord.gg/49T7pVRnfB**

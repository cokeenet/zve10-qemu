# 致谢与参考工作

本项目的许多思路、工具与数据结构认知，直接或间接来自以下公开研究。
**这些项目本身不随本仓库分发**（各自遵循其自身许可）；此处仅作学术性引用与致谢。

如果你认为某处引用不准确、描述有误，或你的工作应当出现在这里却被遗漏，
欢迎提 issue，我们会尽快修正。

---

## 1. Lost-week-1025 / CXD90045-FPSU

针对 **CXD90045 平台**（本机型所用 SoC）的公开研究整理。为本项目在
平台层面（启动链、固件组织、平台服务）提供了重要参考与对照。

- 项目：`Lost-week-1025/CXD90045-FPSU`

**本项目据此获得的帮助**：平台结构对照、术语与分层理解的起点。

---

## 2. DavidBuchanan314

Sony 相机固件逆向方向的公开工作，代表作包括 *ILCE-7M4-RE* 与
*Sony-ILCE-7M4-Linux*：前者是 A7 IV 固件的逆向研究，后者研究在该机身上
运行 Linux 的可行性。两项工作展示了**如何系统性地拆解 Sony 相机固件、
以及如何在其硬件上跑自己内核**的方法论。

- 项目：`DavidBuchanan314/ILCE-7M4-RE`、`DavidBuchanan314/Sony-ILCE-7M4-Linux`

**本项目据此获得的帮助**：固件分析的整体方法论、以及在 Sony 相机上
"把 Linux 跑起来"这一目标的可行性判断与工程路径参考。

---

## 3. ma1co

Sony 相机工具链领域最重要的公开工作之一：

- **`Sony-PMCA-RE`** —— Sony 相机远程控制 / 应用安装（PMCA）研究与工具集，
  是很多人接触 Sony 相机固件的入口
- **`pmca-qemu`** —— 用 QEMU 运行 Sony 相机的早期尝试
- **`fwtool`** —— 固件包解析 / 解包工具的**上游维护者**

> **归属更正（2026-11-03）**：本项目此前在 `docs/FIRMWARE_REPORT.md` 中把
> `fwtool.py` 记为 `DavidBuchanan314/fwtool.py`，**归属不完整**。
> 实际情况是：`fwtool` 源自 **nex-hack**，由 **ma1co** 维护上游，
> `DavidBuchanan314/fwtool.py` 是**下游分支**（两者都在本项目的参考集中）。
> `docs/FIRMWARE_REPORT.md` 已同步标注。

- 项目：`ma1co/Sony-PMCA-RE`、`ma1co/pmca-qemu`、`ma1co/fwtool`（及同作者相关仓库）

**本项目据此获得的帮助**：固件包解析与解包的工程范式（`fwtool` 的 AES-CBC 密钥
**硬编码在工具内**，这一发现直接推翻了我们此前"升级包路线已关闭"的判断）、
QEMU 仿真 Sony 相机的先行经验、以及"相机应用/服务"层面的接口认知。

---

## 4. chocotan/a7c2-re

a7C II（ILCE-7CM2）的逆向 + **原生 QEMU 相机实验**。该项目已把原固件菜单
显示进原生 GTK 窗口并支持多项操作，其公开的技术记录（`DESIGN.md`、
`BOOT-TROUBLESHOOTING.md`、`RECOVERY-AND-LESSONS.md`）对本项目影响很大：

- **显示链配方**：显式**软件 GPE** 模型 + **Sony LZ77 CPU 解码** +
  OSD 队列 + 帧提交校验（HOSDAP02 容器）+ `ramfb-viewer` → QEMU `ramfb`
- **砖机结论**：动态链接的 `busybox` 决定了"致命 preload 无法软件救援"，
  与本项目独立得出的结论一致
- **可写分区约束**：`/tmp`、`/etc`、`/var` 同属一块小 vfat 分区，写满后全部写入失败

- 项目：`chocotan/a7c2-re`

**本项目据此获得的帮助**：软件渲染路线的可复现配方、显示链分层的正确划分方式、
以及砖机救援可行性的独立交叉验证。

---

## 5. chocotan/a7c2-qemu

a7C II 的 **QEMU 机器模型工程本身**（MIT），与上面的 `a7c2-re`（逆向研究）是
**两个独立仓库**。本项目此前只在 `a7c2-re` 段落里笼统提及，不够明确，此处单列。

- 项目：`chocotan/a7c2-qemu`（技术文档 `docs/tech/01..12`）

**本项目据此获得的帮助**：`docs/analysis/deploy/ZVE10_PORT_ROADMAP.md` 的
移植路线直接对照其路线；`docs/analysis/qemu/A7C2_QEMU_REFERENCE.md` 逐条整理了
其技术文档要点。其"`virt` + DTB 补丁 + `ramfb` + 自建输入设备"与我们的
"自研 `cxd90045` 机型 + 自研 `bionz-streamfb`"是两条并行路线（见
`docs/analysis/system/ROUTE_DIFF_VS_A7C2.md`）。

---

## 6. YuuLuo/a7c2-service-preload

服务模式 / `ld.so.preload` 注入思路的先行工作，为本项目理解
"通过 preload 进入服务模式"这一路径提供了参考，也间接提示了其中的风险
（preload 指向不可加载的库会导致整机无法开机）。

- 项目：`YuuLuo/a7c2-service-preload`

---

## 7. qn06142/zv-e10-re

**同为 ZV-E10 机型**的服务模式路线逆向（Sony-PMCA-RE 的分支，固件 2.02/2.03）。
与本项目互补：他们做的是"系统能启动时"的深度，我们做的是"系统起不来时"的通道。

本项目据此获得的帮助：

- **`/usr` 持久化结论的更正**：我们曾据 `BURIED_TREASURE.md` 早期段落
  （内含**其它相机平台**遗留的 LZP 解压字符串）推断 `/usr` 为压缩分区、
  写入不持久，并扩散进四份文档。作者在
  [issue #4](https://github.com/cokeenet/zve10-qemu/issues/4) 中主动澄清：
  `nflasha15` 是**普通未压缩 ext2**，remount rw 后写入**持久**，
  且该段落是其本人已确认过时、忘记撤回的内容。**该更正直接改变了本项目的
  部署方案取舍**（详见 `docs/EXTERNAL_ZVE10_RE.md` §6）。
- **内核启动参数交叉验证**：`wdt.mode=1 xrstreq=1` + 500 ms 看门狗，
  与我们从 `/setting/KEMCO.TXT` 和真机 `/proc/cmdline` 得出的自我更正一致；
  "BootROM 串口启动路径会清 `TIMER-IP`"的解释解决了我们读到
  `WDT TIMERCTL=0` 时的表面矛盾。
- **`0x20312000` 性质确认**：是描述符表而非 CPU 可见扫描输出，
  与我们的真机测绘（`docs/rounds/REALCAM_GROUND_TRUTH_2026-10-27.md` §7）独立吻合。
- **服务模式可用工具**：`gpioctrl` 的命名引脚能力、`klog`/`alog` 环地址、
  `/setting/mode/preload` 等官方开发者钩子。

- 项目：`qn06142/zv-e10-re`

**特别致谢**：其主动纠正我们的错误结论，并在原仓库中同步撤回过时段落。

---

## 8. dino2048/zv-e10-toolkit

ZV-E10 工具库（MIT）。本项目在硬件通道相关工作上引进了其部分工具与流程
（分工与注意事项见 `docs/TOOLKIT_REFERENCE.md`），`fastboot/` 目录中保留其
许可声明（`fastboot/LICENSE.toolkit-dino2048`）。

- 项目：`dino2048/zv-e10-toolkit`

---

## 9. ChambersXDU/sony-a7c2-research

A7C II（ILCE-7CM2）的取证式逆向，含固件版本 2.01 的分区与运行行为验证记录。

- 项目：`ChambersXDU/sony-a7c2-research`

**本项目据此获得的帮助**：`docs/GEN1_BOOT_HARDWARE_TYPEC.md` 中的启动硬件结论
引用了其验证记录；其 `fwtool` 相关工作也进入了本项目的工具参考集。

> 注：本项目的主参考对象是 `chocotan/a7c2-re`，**不是**本仓库；
> 两者曾在本项目文档中被并列提及，此处明确区分。

---

## 10. ice21415/a6000-firmware-platform

A6000 的离线逆向平台工作，展示了在**无设备**条件下重建固件运行环境的思路。

- 项目：`ice21415/a6000-firmware-platform`

**本项目据此获得的帮助**：离线逆向工作流的参照。

---

## 11. BI2QFA/a6300_updater_emu

A6300 的 QEMU 更新器模拟，属于"用模拟器研究 Sony 更新流程"这一路线的早期实践。

- 项目：`BI2QFA/a6300_updater_emu`

**本项目据此获得的帮助**：更新器攻击面与模拟器路线的对照参考。

---

## 12. QEMU 社区

本项目的仿真工作完全建立在 QEMU 之上（机器模型、SD/eMMC 模型、`ramfb`、
交叉调试设施）。感谢 QEMU 社区长期维护这样一个可修改、可学习的模拟器。

---

## 13. 其他

- 所有公开过 Sony 相机逆向笔记、拆机资料、固件结构分析的个人与社区
- 提供 eMMC/SoC 平台资料与工具链的开发者
- 以及在本项目推进过程中提供交叉验证与建议的同好

---

## 引用方式建议

若你需要在文档中标注本项目的来源，建议同时标注上述项目，
因为本仓库的许多结论是在它们之上继续推进的结果。

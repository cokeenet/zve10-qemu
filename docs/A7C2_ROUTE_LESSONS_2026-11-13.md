# A7C2 路线为什么成、我们为什么不成（只读分析）

> 分析对象（均为只读）：`/mnt/build/a7c2-qemu` @`8bce615`、`/root/fw/a7c2re` @`790195b`、
> `/mnt/build/sonyfw-zve10-qemu` @`42554a6`；对照我们的现状取
> `/mnt/build/m2qemu`（deployed `stage-current/ui.cpio` = `6ec94a1e…`，最新基线 `out/v316`）。
> 本文只写能从文件里读到的东西；读不到的进第六节。
>
> **一句话**：他们的成功**不是"过了首设"**，而是**把绘图后端换成了 guest 内的软件实现，
> 并让固件自己在原来的（已被答复的）等待上继续走**。我们缺的不是首设开关，而是
> ①同代的 Backup 数据源 ②一个能应答"入口等待"的 owner ③把 `GRM_*` 真的执行成像素的后端。

---

## 五个问题的直接索引（细节见对应章节）

| # | 问题 | 一句话答案 | 详见 |
|---|---|---|---|
| 1 | 他们启动是否必须过首设？判据是什么 | **是，首设是必走的（且每次必走）**：判据是模型日期状态(4B)与 force(1B) 组合 `(!force && status!=2)\|\(special` → 入口 `0x347`；但**完成状态不持久化**，所以"过完首设"不是一个可保存的结果 | §2.1 |
| 2 | 我们 `Backup.bin` 的差异是否意味着"首设已完成" | **不是**。`96519cd0` 是 ZV-E10 出厂模板的**字节级原件**（= `RK/CX88700_ALLLANG.bin`），`93205336` 只是同一模板被跑脏；换回干净模板**行为完全不变**（v316 已实测） | §2.2 / §2.3 |
| 3 | 屏幕/LCD 是固件自己画还是外部触发 | **建屏与像素都由固件自己发起**；"外部触发"只有两类：(a) 替固件**答一个它正在等的 Get**（a7c2 唯一的注入），(b) 用**原厂 helper/真实按键**告诉固件进哪个视图。他们产品默认路径里**没有**"强制绘制"的 hook | §3.1 / §3.2 |
| 4 | 他们是否做了自研 appFw 替换 | **做了，但性质是"逐函数源码等价重建"**：`appFw.hybrid.so`（106,463 入口、`2230203f…`）按原入口/原布局保留其余字节、余量填 NOP、整文件读回；`complete_appfw_from_source=false`。而**产品默认冷启动路线明确"原 im/appFw 未替换"** | §6 第 9 条 |
| 5 | `menu_shell.c` / `native_entry.h` 是辅助还是替代固件绘制 | **既不是辅助也不是替代固件的原生绘制**：它们只在**可选 A7C2 资源模式**下存在，只给"我们新增的那一行"画字（`x=24,y=332`），文档自称"**我们的渲染器**，不是原 AP 的标签管线"；默认原版模式根本不编译它们 | §6 第 10 条 |

---

## 一、他们成功的完整启动路径（分步骤，逐条证据）

a7c2-qemu 的产品入口是 `./start.sh`；它不是 `-M zve10`，而是
**`-machine virt,secure=off -cpu cortex-a35 -smp 1 -m 24576`**
（`docs/tech/02-qemu-boot.md:88-89`）。他们的固件是 A7C2 2.01（AArch64，`appFw.so` 99,960,744 B，
`docs/tech/12-debugging.md:17-19`），与我们的 ZV-1M2（ARM32，`appFw.so` 64,273,528 B）**不是同一 ISA、
不是同一代 UI 架构**。路径为：

| # | 步骤 | 做了什么 / 证据 |
|---|---|---|
| 1 | 原内核进 shell | 固定装载（DTB `0x400002000`、kernel `0x400080000`、initrd `0x440000000`），用 libfdt 改 `/chosen/bootargs`，补 GIC/UART/timer 别名；未建模 initcall 走 `initcall_blacklist`。`docs/tech/02-qemu-boot.md:60-70`；GIC/UART 别名与 4 个 timer、SPAcc、总线兜底的源码在 `source/qemu-sony-native.patch`（`hw/arm/virt.c` 段、`hw/misc/sony_cxd_bus_stub.c`）。判据只认串口 `/ #`（`02:103-110`） |
| 2 | 原 `init → setup_rootfs → pivot_root → chroot → freeramdisk → init2` | 原脚本字节绑定，7 项原脚本 SHA 与官方一致；`docs/tech/2026-11-09-original-cold-boot.md:1-7,39-52` |
| 3 | `init2 → bootin → im → appFw`，**原 ELF 不替换** | `cold/bootin.sh … 最后 exec 原 bootin，原 im/appFw 未替换`：`docs/tech/2026-11-09-original-cold-boot.md:47` |
| 4 | **清掉阻塞**：让原 `sequence_boot` 的 Dataflow pin Get 得到应答 | 早期黑屏根因不是版本数据，而是 pin 拓扑等待；开启三个**只读 Get owner** 后原 sequence 自己走完：`a7c2re/report/2026-10-17-qemu-full-system/OFFICIAL-BOOT-BLOCKER.md:38-53`；owner 表（InfraBB 21 项 / DRAWINFO 2 项 / OsdPinIF 1 项 + SID `9080003b`）、GRM Get 的两条特殊语义与原侧消费证据（`waiter=1 rc=0`）在 `a7c2re/report/2026-10-19-official-first-setup/REPORT.md:36-56` |
| 5 | 原固件自己建屏并画首设页 | `view::create_screen` 槽位是运行期 VA `base+0x58dbf38`（**不是**文件偏移 `0x58cbf38`）：`runtime/analysis/menu-observe.c:1465-1470`、`6520-6524`；首帧后才有 `CREATE_SCREEN_ARGS`（`05-display.md:11-13`） |
| 6 | 绘图请求真的落到位图 | `libgpe.so` 的 5 个 PLT 槽被换成软件光栅后端：`menu-observe.c:1366-1388`（`Init/FillRect/BitBlit/Flush/GpermInit`）；格式为显式 RGBA4444 软件模型（`05-display.md:23-42`） |
| 7 | 资源真的解码 | `libgpe` 之外的另一处：`/dev/ldec` 缺席时 `QEMU_SOFTWARE_LZ77` 走 CPU 解码，先校验再写原位图：`menu-observe.c:12010-12024`；`05-display.md:44-55` |
| 8 | 帧被复制、提交、通知完成 | 挂在原 LIRO 边界的 headless OSD，按 `0x1002 → 0x1003/0x1005` 配对，持租期复制，产出 HOSDAP02 自校验容器：`05-display.md:57-94` |
| 9 | 像素交给 QEMU 显示 | `ramfb-viewer` 校验容器 → XRGB8888 → 写 `[0x41e100000,0x41e600000)` → `fw_cfg` 配 `etc/ramfb`；QEMU 侧由 `-device ramfb,id=a7c2-screen`（`runtime/analysis/qemu-kernel-probe.sh:64`）承接：`05-display.md:96-108` |
| 10 | 真实输入驱动首设 | 20B 输入前端发 `DOWN/ENTER/DELETE`，逐页核"原屏内容 + 输入双边消费"；`04-app-and-first-setup.md:40-46`，页序与 445s 到拍摄主界面见 `a7c2re/report/2026-10-19-official-first-setup/REPORT.md:110-118,167-200` |

**关键点**：第 4 步是**唯一一处"不是等固件、而是替固件答一个 Get"**；第 6/7 步是**替固件做后端**；
其余全部是原固件自己执行。他们的 README 也承认取景仍黑、拍摄未通（`README.md:5`），
**成功范围只是"菜单/HUD 的 AP 软件 OSD"**。

---

## 二、首设/初始化向导：判据、进入条件、我们是否漏了这一整步

### 2.1 他们是否"必须"经过首设

是——但**首设不是"解锁绘制的开关"，首设本身就是要被绘制的东西**。

- 进入判据（a7c2 原字节已核）：`process_power_on` 读 **模型日期状态(4B)** 与 **force 状态(1B)**，
  按 `(!force && status!=2) || special` 选择首设入口 `0x347`；另有一条特殊分支不能省
  （`a7c2-qemu/docs/tech/04-app-and-first-setup.md:26-31`；原字节位置见
  `a7c2re/.../report/first-setup-frontier.md:10-19`）。
- **完成状态不持久化**：四份已核 Backup（公开 seed / 真实首设 run / snapshot / 第二轮）里
  `02cf0014`、`01070140` 均为 `01`，而完成真实首设后 **模型 force 仍为 1、日期状态为 0**
  （`first-setup-frontier.md:23-25` 与两阶段实测表 `:46-59`）。
  ⇒ **"把某个字节写成 1 跳过首设"这条路在他们的证据里也不成立**；
  `04-app-and-first-setup.md:33-36` 直接写"当前每次无恢复输入的启动都会重走首设"。
- 而且 `0x347` ≠ 实际语言页：日期分支选 347，随后 `1b6b224(0x311)` 非零会选实际语言页 `0x12d`
  （`first-setup-frontier.md:71-83`）。所以"看到语言页 ⇒ 347 分支命中"是过宽结论。

### 2.2 我们的 `Backup.bin` 差异意味着什么

实测（本机现算）：

| 文件 | SHA-256 | 大小 |
|---|---|---|
| 我们 `work/initramfs-ui/setting/Backup.bin` | `93205336…` | 1,235,836 |
| 我们 `Backup.bak`、**参考树** `work/initramfs-ui/setting/{Backup.bin,Backup.bak}` | `96519cd0a4b197d2…` | 1,235,836 |

**已把 `96519cd0` 追到源**：它就是 ZV-E10 固件包里的
`0110_backup/SYSASTRA-DSLR/RK/CX88700_ALLLANG.bin`，**逐字节相同、SHA 完全相同**
（`MainDest.txt` 指定的机型文件也是 `CX88700_UC2.bin`，同属 RK 家族）。
⇒ `96519cd0` = **ZV-E10 出厂模板原封不动**，不是"某个更高级的、首设已完成的配置"。
`93205336` 则是**同一份模板被固件跑过之后写脏的副本**（见下）。

逐字节差异矩阵（本机现算）：

```
96519cd0 (RK/CX88700_ALLLANG.bin, 出厂原封)   vs 93205336 : 147,768 字节不同 (11.96%)
7bec3cd9 (RK/CX88701_ALLLANG.bin, 另一出厂变体) vs 93205336 : 147,741 字节不同 (11.95%)
96519cd0 vs 7bec3cd9（两个出厂变体之间）      :      51 字节不同 (0.00%)
```

**结论（三条，都是否定的）**：
1. `96519cd0` **是干净出厂模板**（`CX88700_ALLLANG.bin` 的字节级副本）；`93205336` 是"这份 E10 模板被
   M2 固件跑过、写脏了"（差异集中在 DB 记录区，且含运行期计数器/CRC 变化）。
   差异**不代表"首设已完成"**，只代表"这份 DB 被应用反复写过"。
2. 参考树之所以是 `96519cd0`，是因为**它就是 ZV-E10 项目**，其解包脚本把**它自己机的**
   出厂模板原样复制成 `Backup.bin`/`Backup.bak`（`sonyfw-zve10-qemu/docs/setup.md:55`、
   `scripts/prepare-firmware.py:33,146-151`）——**无条件复制、不做任何首设相关处理**。**这恰好证明"参考树用的就是一份未经任何首设的出厂模板"**
   ——也就是说：**"干净模板 → 出菜单"在他们那里成立，而我们要的不是"更干净的模板"。**
3. 我们已经做过这个单变量实验并已记录：用 `96519cd0` 替换后行为**完全不变**
   （`oss/docs/QEMU_M2_PROGRESS.md:57056-57097`，v316：行数正常、`createLayer=6`、
   GPE 仍 19 包、`FRAMEDUMP nonzero16=0`）。本次复核 v316 产物一致
   （`/mnt/build/m2qemu/out/v316/artifact-sha256.txt` 的 ui.cpio = `6ec94a1e…`，
   `qemu.err`：`ZVE10_GPE_PACKET` 19 条、`FRAMESCAN … best_nonzero16=0`、`FRAMEDUMP … nonzero16=0`）。

### 2.3 "怎么让固件重新进入首设"——按他们的证据，这条不是可操作的杠杆

- 首设进入靠**模型状态**（4B 日期状态 / 1B force / 特殊分支的 key、ext_mode、另一模型），
  而这几项**不是 Backup ID/SID**，是"另一编号域"（`first-setup-frontier.md:19`）；
  他们的 C 探针只能只读采集，且明确"不得调用日期 setter 或 sync、不写完成标志"（同文 `:29-33`）。
- 我们这边，UI 线程启动期最后一次可观测动作是**一次失败的 Backup 读**：
  `BACKUP_nread n=16 key=0x2cf1844 len=1 ret=-1 tid=195 caller=/usr/lib/appFw.so+0x2bc41c2`
  （`out/v316/serial0.log:5641`）。该 key 在**三份模板里都不存在为 32 位字面量**
  （E10 模板 / M2 模板 / 我们的脏 DB 均为 0 次命中，本机实测），
  且用固件自己的 `Backup_write` 去造这两把 key 已被证否（`BACKUPSEED … -> -1 / -4`，
  `out/v57|v58|v59/serial0.log`；代码与推理见 `m2qemu/src/argtrace.c:6077-6097, 9388-9413`）。
  ⇒ **"补一条首设标志"在本固件上没有落点**。

---

## 三、屏幕创建与首次绘制的触发机制（谁发起、什么条件）

分两层回答，因为三个仓库的答案不同：

### 3.1 屏幕（screen/view）本身：永远是**原固件自己创建**

- a7c2：`view::create_screen` 由原 vtable 槽 0 执行，观测器只是把该槽换成 wrapper 记录
  （`menu-observe.c:6414-6417`、`1465-1470`、`6520-6545`）。建屏记录 `0x308/count28` 是
  原固件自然产生的（`a7c2re/.../BOOT-TROUBLESHOOTING.md:82` 第 2 步注）。
- 我们：**没有 `create_screen` 这个符号**，是 `WidgetSystem/createLayer` 模型；
  v316 里 `CREATELAYER n=1..6` 全部返回非 0（`out/v316/serial0.log:4987-5392`）。

### 3.2 首次绘制的"外部触发"：存在，但**不是**他们产品默认路径里的那一环

| 触发手段 | 在谁身上 | 是产品默认还是研究工具 | 证据 |
|---|---|---|---|
| 受控九参入口 `appFw+0x1e78870`，把 308 屏切到 346 菜单 controller | a7c2 | **研究工具**；文档明写"受控入口是定位工具，不是自然冷启动已恢复的证据" | `menu-observe.c:11601-11676`；`docs/tech/05-display.md:19-22`；`BOOT-TROUBLESHOOTING.md:83` |
| 直接 `WidgetSystem::notifyDraw()` | a7c2 | **研究工具**，`MENU_UX_NOTIFY_DRAW` 门控 | `menu-observe.c:6119-6135`、`6442-6444`、`6873-6894`、`6951`（`MENU_BB_READY_INJECT`） |
| 真实 MENU 键 `0x1ab` 按下/释放 → 原固件切到菜单屏 | a7c2 | **产品默认** | `a7c2re/.../GUI-MAIN-MENU.md:14-16`（key 0x1ab 事件 75/76、切到 `id=714 count=44`） |
| 在 UI 线程、首帧 `drawFramebuffer` 之后调用**原 RootSelector helper**（`views_base+0x47de27`，前置 `+0x590f35` Wi-Fi/mute guard，校验 `0xa3` 实例三字段） | sonyfw-zve10-qemu | **产品默认**（默认原菜单就是这样打开的） | `source/ui/ui-observe.c:71`（在 `observe_draw` 尾部调用）、`:254-281` |
| 通过原 selector thunk `+0x41a7d9` 依次请求 `0xa4/0x34/(0xc3,3)` 打开拍摄页 | sonyfw-zve10-qemu | 显式 opt-in（`--shoot-view`），且研究性 | `source/menu/original.inc:900-925`；`docs/shoot-model-assessment.md:15,18` |

**所以"屏幕怎么开始画"的答案是两层**：`create_screen`/`createLayer` 与像素绘制**由固件自己发起**；
外部能做的只有两件事——(a) **替固件应答它正在等的 Get**（a7c2 唯一的"注入"，
`OFFICIAL-BOOT-BLOCKER.md:38-52`）；(b) **告诉固件"进入哪个视图"**（原 helper 或真实按键）。
sonyfw 那样"调原 helper 打开菜单"之所以安全，是因为它**只调用原厂的 thunk 并保留原 guard**，
不是自己造对象。

一个必须记的操作性细节：他们的**晚到字形绘制**晚于"两帧相同"的判定——
提交 `42554a6` 把 `settle()` 从"两帧相同"改成"**三**帧相同"，理由写在函数 docstring：
`late font draws can follow one equal pair`（`scripts/menu_run.py:293-300`，回归
`tests/test-native-menu.py:237+`）。⇒ 任何"等画面稳定"的判据都必须 ≥3 帧。

---

## 四、他们有而我们没有的组件/模型/配置（逐条）

**A. 结构性（不是配置能补的）**

1. **`libgpe.so` 这个独立的 2D 引擎模块**。a7c2 把 5 个 PLT 槽换成软件后端正是因为存在这个库
   （`menu-observe.c:1366-1388`）；其资产 pin 为 `80fc6236…`（`runtime/analysis/prepare-assets.py:49`）。
   **我们的树里根本没有 `libgpe.so` / `libgrm.so`**（`work/initramfs-ui/usr/lib` 221 个 so 中不存在；
   出厂也是幽灵注册，`oss/docs/QEMU_M2_PROGRESS.md:12855-12864`）。我们对应的面在 `libObj.so`：
   `GRM_gpermFillrect@0x117b4d`、`GRM_gpermRectblit@0x117c87`、`GRM_bitmapCreateFromPhys@0x114d99`、
   `GRM_screenGetOnBitmap@0x115681`，而 `GRM_fbrm*` 家族是 `mov w0,#0; ret` 空壳
   （`a7c2re/report/yc-cpu-consumer-boundary/README.md:33`）。**⇒ "替换后端"的目标函数不同，
   不能照抄 pin，但"把这两个 gperm 入口在 guest 内改成真光栅"是可移植的。**
2. **ISA / 机器模型**：他们 AArch64 `virt+cortex-a35`；我们 ARM32 `-M zve10`（A9 MPCore）。
   他们的 `.so` 不能进我们的树，反之亦然（`a7c2re/report/2026-11-07-zve10-a7c2-menu/README.md:36-37`
   明确"整体移植 A7C2 appFw 与运行库 = ARM64/ARM32 不兼容，不作为起点"）。
3. **同代的 Backup 数据源**：a7c2 用的是**它自己机型的**模板
   `0110_backup/SYSIPSX-DSLR/PP/CX89701_ALLLANG.bin`（`prepare-assets.py:25`，且 seed 与
   `seed-setting/Backup.bin` 同源同 SHA，`docs/tech/08-settings.md:24-38`）。
   **而我们用的是 ZV-E10 的 `CX88701_ALLLANG.bin`**。**重要更正**：ZV-1M2 固件包**自带**备份模板——
   `/mnt/build/fw/unpacked/unpacked_zv1m2/firmware.tar_unpacked/0110_backup/SYSASTRA-DSC/YS/` 下有
   `CX64801_ALLLANG.bin`（1,305,736 B，`03917a09…`）等 10 份，`MainDest.txt` = `CX64800_UC2.bin`
   （`21 75 5a 06…`）。所以 `oss/docs/QEMU_M2_PROGRESS.md:11631-11634` 的
   "ZV-1M2 固件**不自带** Backup.bin" **是错的**，我们从未试过同代模板。
4. **pin 拓扑的只读 owner**：a7c2 的 `--infrabb-service / --drawinfo-service / --grm-pin-service=registry`
   （`OFFICIAL-BOOT-BLOCKER.md:38-47`）。我们**无法照搬**：`0x9080003b` 在 Gen5 `appFw` 只有 1 处、
   `libObj` 0 处（`oss/docs/QEMU_M2_PROGRESS.md:51303`）；但我们的 `appFw` 仍有 87 个
   `Infra*`/69 个 `DataflowInfra*` 未定义符号，说明我们也有同类等待面，只是编号不同。

**B. 模型/工具层（可照抄的部分）**

5. 软件 LZ77 后端（替 `/dev/ldec`）——我们已有对应开关但**没在本固件验证过**
   （`MENU_SOFTWARE_LZ77` 概念对应 a7c2 `menu-observe.c:12010-12024`；我们部署树里
   `usr/kmod/ldec.ko` 与 `argtrace.so.ldecprobe` 都在，说明探测做过）。
6. headless OSD 的 1002/1003/1005 配对 + 租期复制 + HOSDAP02（`05-display.md:57-94`）。
   我们用的是**另一条**：QEMU 侧 `ZVE10_FRAME_OUT/FRAMEDUMP/FRAMEPERIOD` 定时采样 +
   `qsrc/hw/arm/zve10.c:190-380` 的 host 端直接读物理窗口，**没有 guest 内带租期的提交容器**。
7. `ramfb` 桥 + `memrsv` scanout 预留（`05-display.md:96-108`；`qemu-kernel-probe.sh:64`）。
   我们的 `-M zve10` 模型里**没有 ramfb**，只有 host 定时 pmem 采样。
8. QEMU 设备桩：`sony_spacc_stub.c`、`sony_cxd_bus_stub.c`（读全 `0xFFFFFFFF` 的兜底）、
   `sony_hwtimer.c`、`sony-ui-input.c`（host 按键桥）（`source/qemu-sony-native.patch`）。
   注意 a7c2 自己也**没有**模拟 GPE/GPU 硬光栅——GPE 是软件模型。
9. 持久内部盘 + 设置实例/CAS 代数（`docs/tech/08-settings.md:1-38`）：他们能"恢复输入"的前提是有
   受校验的代与租约。我们是 cpio 里一份裸 `setting/Backup.bin`（无代、无校验链）。

**C. 我们这边实测到的、他们那边没有的坑**

10. **`dma330_stub` 不见了**：当前部署 `work/initramfs-ui/init`（→ `ui.cpio 6ec94a1e…`）**没有**
    `load /usr/kmod/dma330_stub.ko`（与 `init.bak` 的 diff 可见），因此
    `liro.ko` 直接 `Unknown symbol dma330_mem_transfer_async (err 0)`
    （`out/v316/serial0.log:1255-1257`）；v303/v310/v314/v315/v316 全部如此。
    而做 av-cam 单变量对照的 **v305/v306 是加载了 stub 的**（`out/v305/serial0.log:336-337`、
    `LIRO (liro) rev:4-MUSASHI`）。⇒ **我们现在的"基线"和"对照实验"不是同一套 init。**
11. 我们的 `-M zve10` 模型**只完成一个捕获到的启动 fence**，注释写明
    "not a general GPU interpreter"（`/mnt/build/sonyfw-zve10-qemu/source/qemu/hw/arm/zve10-gpe-init.h:6-10`）；
    v316 里 `ZVE10_GPE_PACKET` 19 条内容完全相同、`ZVE10_GPE_INIT_FENCE` 19 条，与之相符。

---

## 五、对移植 ZV-1M2 的可执行建议（按优先级）

> 排序依据：**单变量、可证否、不需要 GPU 模型**。

### P0. 换成 ZV-1M2 自己的备份模板（最高优先级，成本最低）

- 做法：把 `/setting/{Backup.bin,Backup.bak}` 换成
  `CX64801_ALLLANG.bin`（1,305,736 B，`03917a09…`）/ `CX64800_UC2.bin`（`MainDest.txt` 指定的那份），
  保持 `backup.ko`=M2 原版（`b0fa9d81…`，已验证与我们部署的一致）。
- 预期收益：这是**第一次**让"数据代"与"固件代"一致。当前我们给 M2 的 `backup.ko` 喂的是 E10 的
  DB（11.95% 内容不同），`BACKUP_nread` 对 `0x2cf1844/0x2cf1845` 直接失败——同代 DB 是唯一
  没试过的分支。
- 风险：`backup.ko` 可能在 insmod 时 `Input/output error (status=5)`（历史上 `Backup_load=0`
  就是这样，`QEMU_M2_PROGRESS.md:11625-11633`）⇒ 那就是**明确的负面结果**，正好收窄边。窗口用 600s。
- 判据（写死，不许放宽）：`MODULE_END /usr/kmod/backup.ko status=0` + `createLayer=6` +
  `BACKUP_nread key=0x2cf1844 ret≥0`（这条是本轮唯一的"门"读数）+ `FRAMEDUMP nonzero16>0`。

### P1. 把 `dma330_stub` 放回 init，恢复 v306 配置再谈基线

- 做法：`init` 里加回 `load /usr/kmod/dma330_stub.ko`（与 `init.bak`/`init.15mods.bak` 对齐），
  重打 cpio，单一变量跑一次。
- 预期收益：消除"基线 ≠ 对照配置"这个测量漏洞；liro/MUSASHI 才真正在跑。
- 风险：v305 证明 stub + av-cam 存在时主流程停在 t=120 ⇒ 与 av-cam 共存仍会互斥。
  但这正好是我们要**单独确认一次**的事实（v306 移除 av-cam 才到 t=1125）。

### P2. 照抄 sonyfw 的 `settle()` 判据（≥3 帧相同）

- 做法：任何"画面稳定 / 有字"的判据都改成连续三帧字节相同（`scripts/menu_run.py:293-300`）。
- 预期收益：避免"晚到字形"被误判为未绘制（这正是他们 `42554a6` 修掉的假阴性）。
- 风险：判定变慢；对"从不绘制"的用例无影响（我们目前正是这种，所以零风险）。

### P3. 用 a7c2 的"找等待者"方法，而不是继续加 hook

- 做法：把 UI 线程那条链按 a7c2 `FORCEWRAP` 的方式**逐跳观测**而不是强值：
  `0x258eac8 → {0x2bc41a8(BK), 0x279a30c, 0x23bde34, 0x23de1ec, 0x23bde64, 0x279bdb0}`
  （`m2qemu/src/argtrace.c:4790-4820` 已有 pin 与 trampoline 方案）——
  目标改成**"这 6 跳里谁不返回"**，而不是"把返回值改成 1"。
- 预期收益：我们已知该包装器会在 getter 之后停（`BACKUP_nread n=16 … ret=-1`，
  `v316/serial0.log:5641`），那么下一个 problem 就在 6 个下游之一；这是**唯一还没做的观测**。
- 风险：hook 密度会影响可达率（我们自己的 v538 层教训：6/6 零帧与探针无关，
  但可达率受影响）。用 `PROBE_NONE` 做对照各跑一次。

### P4. 把"重进首设"作为**独立实验**，不要再当作主线

- 依据：a7c2 侧结论是"首设完成不持久化、每次启动都重走首设"（`04:33-36`），
  我们能改的只有模型状态，而这些字段**不在 Backup 编号域**（`first-setup-frontier.md:19`），
  且我们已实测换干净 DB 无变化（`QEMU_M2_PROGRESS.md:57056-57097`）。
- 结论：**不要**为"进不了首设"再投入；把首设当作"能画之后自然出现的东西"。

### P5. 走 sonyfw 的"软件后端替换"策略，但换到我们的符号上

- 做法：在 guest 内把 `libObj` 的 `GRM_gpermFillrect@0x117b4d`、`GRM_gpermRectblit@0x117c87`
  改成真光栅（RGBA4444 + 裁剪 + 最近邻），把 5 个 `GRM_fbrm*` 导入保持**只读探针**。
- 预期收益：这是唯一与"有没有 GPU 模型"无关的像素来源；a7c2 与 sonyfw 都是这条。
- 风险（必须诚实说）：**我们目前没有任何证据表明固件会走到 `GRM_gpermFillrect`**——
  `GRMRegisterTrigger`/`drawFramebuffer`/`notifyDraw`/`updateFrame` 的 PLT 槽在 v316 全部是
  `PLT0(never-called)`（`v316/serial0.log:5432-5440`）。所以这条应排在 P3 之后，
  否则很可能"后端做好了，没人调用"。

---

## 六、明确无法确定的部分（不猜）

1. **"a6400 两天内跑出 A7C2 原生新菜单"这件事本身**：三个仓库里 `a6400` 只在
   `devices.yml`/pin-json 里作为机型串出现，**没有任何 a6400 机器模型或该实验的记录**；
   而 a7c2-qemu 自述是 **A7C II（CXD90057，AArch64）** 跑在 `virt+cortex-a35`。
   ⇒ 无法核实该传闻，也无法判断传闻里的路径与 a7c2-qemu 是否同一条。
2. **我们的固件是否也存在"Dataflow pin Get 等待"**。我们只知道 `0x9080003b` 在 Gen5 `appFw`
   出现 1 次、`libObj` 0 次（字面量计数），**没有做运行期 owner 分析**；不能据此断言
   "Gen5 不是 pin 拓扑"。
3. **"UI 主线程停在失败的 `BACKUP_nread` 之后"这个说法本身也只是观测窗口边界。**
   v316 的 `BACKUP_nread n=16 … ret=-1` 在 `serial0.log:5641`，而全文到 `:5656` 才截断，
   之后再无 tid=195 的记录——**这与 v303 那次"只差 6 行"是同一个陷阱**
   （我们自己的 ledger 第 662 章已为此撤回过一次结论，`QEMU_M2_PROGRESS.md:55883-55900`）。
   ⇒ 只能说"窗口内没再观测到"，不能说"它停住了"。要做这条结论，**必须先把日志上限
   抬到覆盖 t≈1100s 之后**，或者改用 QEMU 侧线程采样（`TS` 行）而不是日志末尾。
4. **`updateFrame`/`isFrameDelay` 探针 0 命中 与 `DRAWSTAGE enter 6 =1` 并存的原因**。
   两套探针落在不同层（PLT 槽 vs 内层函数），我们没有同一次运行里对两者的 ABI/时序做一致化
   验证；这属于测量边界问题，不能推出"固件没进入绘制循环"之外的更强结论。
5. **M2 自己的备份模板会被 M2 的 `backup.ko` 接受吗**。本轮是只读分析，没有跑 QEMU；
   1,305,736 vs 1,235,836 的尺寸差异是否触发备份驱动内部校验，未知。
6. **"completing 首设"在本固件是否真的存在一条可写路径**。他们的两阶段实测显示完成后
   force 仍为 1、日期状态为 0，我们自己也没有看到任何 setter 被调用；
   这既不能证明"没有 setter"，也不能证明"有"。
7. **`PRIVACY_NOTICE_ACCEPTED_DURING_INITIAL_SETUP` 是否可达**。该字符串确实在我们的
   `appFw.so` 里（与 `PRIVACY_NOTICE_NOT_ACCEPTED`、`BKID_APP_is_accepted_privacy_notice`、
   `asrid_func_privacy_notice` 同族，共 7 处 privacy 串）；但 a7c2 的 457 隐私页移植是
   **另一个独立页面移植项目**（`a7c2re/report/2026-11-05-appfw-source-rebuild/PRIVACY-PAGE-PORT.md`），
   它明写"当前 457 行仍是灰显待移植、`configuration_written=false`"，且从未声称这是首设开关。
   ⇒ **不能把"我们有这个字符串"当成"首设有隐私页门禁"的证据**。
8. **他们是否真的"两天"**。仓库自身的时间线是 2026-10-17 → 2026-11-09，8 个步骤、
   106,463 个源码入口、上万项测试；README 也仍未承认成片（`README.md:5`）。
   与"两天"不符，但这是文档层面的观察，不是反证。
9. **a7c2 的 hybrid `appFw.so`（`2230203f…`，106,463 入口）到底"替换了什么"**。
   `replace` 模式的合同写得很清楚：保留原入口、原布局、其余字节，余量填 NOP、整文件读回
   （`a7c2re/report/2026-11-05-appfw-source-rebuild/README.md:92-107`），且
   `complete_appfw_from_source=false`；而产品默认路径又明确"原 im/appFw 未替换"
   （`a7c2-qemu/docs/tech/2026-11-09-original-cold-boot.md:47`）。
   ⇒ 它是**字节级等价的逐函数替换（用于源码可读性）**，不是"用自研固件替代原厂固件"；
   但"默认路径是否某次真的用它跑过首设+HUD+MENU"（`GUI-MAIN-MENU.md:4-20`）与默认产品路线
   的关系，两个仓库的表述不完全一致，无法从文件层面判定。
10. **`menu_shell.c` 在出帧链里的角色**（问题 5 的正面回答）：它**不是**"替代固件绘制"，
   也**不是**"辅助固件的原生绘制"——在 sonyfw 里它有且只在**可选 A7C2 资源模式**下存在，
   职责是给"我们新增的那一行"画字（`x=24,y=332`），文档明写"它是**我们的渲染器**，
   不是原 AP 的标签管线"（`docs/menu-modes.md:30`）；默认原版模式**根本不编译**
   `menu_shell.c`/`native_entry.inc`（`docs/menu-modes.md:18-19`）。
   ⇒ 若判据 1 要求"像素必须出自原厂代码路径"，那么**sonyfw 的原菜单帧符合，但它的 A7C2 新行文字不符合**；
   `a7c2re/report/2026-11-07-zve10-a7c2-assets/README.md:22` 也承认该原型
   "没有实例化原 AP 的完整 44 控件，也没有恢复原设置执行、原面板输入或原 TextRM renderer"。

---

## 附：本次用到的实测命令（可复现）

```bash
# Backup 三代对照
sha256sum /mnt/build/m2qemu/work/initramfs-ui/setting/Backup.{bin,bak}
sha256sum /mnt/build/fw/unpacked/unpacked_zve10/firmware.tar_unpacked/0110_backup/SYSASTRA-DSLR/RK/CX88701_ALLLANG.bin
cmp -l  CX88701_ALLLANG.bin  work/initramfs-ui/setting/Backup.bin | wc -l   # 147741
cmp -l  CX88701_ALLLANG.bin  work/initramfs-ui/setting/Backup.bak | wc -l   # 51

# 我们的基线到底跑了什么
head -2  /mnt/build/m2qemu/out/v316/artifact-sha256.txt
grep -c "ZVE10_GPE_PACKET" /mnt/build/m2qemu/out/v316/qemu.err                 # 19
grep "FRAMESCAN\|FRAMEDUMP" /mnt/build/m2qemu/out/v316/qemu.err                 # best_nonzero16=0 / nonzero16=0
grep -n "PLT0(never-called)" /mnt/build/m2qemu/out/v316/serial0.log | sed -n '1,9p'
grep -n "liro: Unknown symbol" /mnt/build/m2qemu/out/v316/serial0.log            # :1257

# 缺 libgpe / 有 GRM_*
ls /mnt/build/m2qemu/work/initramfs-ui/usr/lib | grep -c 'libgpe|libgrm'
readelf -sW <(cpio -i --to-stdout usr/lib/libObj.so < stage-current/ui.cpio) | grep GRM_
```

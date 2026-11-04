# New menu port — current status

[English](#english) · [简体中文](#简体中文)

Last updated: 2026-11-05. Supersedes the conclusions in
[`MASTER_HANDOVER_NEWMENU.md`](MASTER_HANDOVER_NEWMENU.md) (2026-10-28) where they conflict.

---

## English

### The objective

Run the **ZV-1M2 menu stack** (CXD90045_k8 generation) on a **ZV-E10**. This is not a settings tweak:
the two generations have structurally different UI engines.

| | ZV-E10 (older) | ZV-1M2 (newer) |
|---|---|---|
| `libObj.so` | 21 MB, **exports** `Application_System_Init` | 3.9 MB, does **not** export it |
| UI shape | single layer | two stages: `appFwBoot` (8 KB) → `appFw` (64 MB) → `Application_Start` |
| Init entry | `Application_System_Init(144 B)` | `uxengine_init2(216 B)` |

### Where it stands

Measured in QEMU; no claim here without a log line behind it.

```
Hybrid /usr tree built and booting                     ✅
INITSEQ entries resolved: 66                           ✅
Application_Start entered                              ✅
Graphics packets submitted (GPE): 19                   ✅  (first real GPU activity)
Frame captured from our own process: 0                 ❌
```

**There is no frame. There has never been a frame.** An earlier entry claiming the new menu had
rendered was retracted on 2026-10-28: the capture helper used `pgrep -x qemu-system-arm | head -1`,
two emulators were running, and the frame belonged to a different session's old-menu process. Re-measured
with the exact PID recorded at launch, the result was 0.0% non-zero every time. The "1-in-6
intermittent rendering" reported alongside it never existed either.

### Three blockers

Each is pinned to a specific interface, not "it hangs somewhere".

#### 1. Backup service queue `0x3e014d` has no receiver

- `osal_valloc_msg_wait(0x3e014d, …)` **succeeds** — the queue exists and accepts allocation.
- No receive entry point exists among the **62** observed registrations, and `libbackup.so` imports only
  `osal_reg_msg_queue_cb` / `osal_unreg_msg_queue_cb`.
- `backup.ko` must `file_load` a valid `/setting/Backup.bin` **at insert time**; the firmware package
  ships none (`0800_appli/setting` has only `DmmConfig.bin`, `facedmy.bin`, `kemco.txt`, `ulogio.bin`,
  `appfw_infra`), so it is generated on first boot.
- `Backup_load=0` ⇒ `insmod` fails outright (`Input/output error`, status 5) and takes `ulogio`, `dmm`,
  `udate` down with unresolved `Backup_read*` symbols.
- Faking the reply (`backup_sync_msg` returning success with `*out=0` or `*out=1`) stops the boot at
  `MWF_Init` (~452 log lines).
- Three `Backup.bin` variants tested (firmware default, real-device generated, hand-edited version
  string) — all load, none unlock the service.

**What would resolve it:** a genuinely first-boot-generated `/setting/Backup.bin`, or code that makes the
registrant start, or understanding why nothing registers the queue.

#### 2. Graphics device model incomplete

- `grm_gles.ko` requests a threaded interrupt on **IRQ 76** (`IRQF_DISABLED|IRQF_TRIGGER_HIGH`, `0x84`).
- `sugilite_interrupt` reads status at `bar + 0x34`; a **zero read triggers a kernel BUG**.
- Status bits: `0x80000000` → clear `bar+0x1000`; `0x40000000` → clear `bar+0xC18`;
  `0x04000000` → clear `bar+0x1C`; low 4 bits → write 1 to `bar+0x3C` and `bar+0x44`.
- The completion mask the engine waits on is **bit 26** (`0x04000000`).
- Our device model (`/mnt/build/m2qemu/qsrc/hw/arm/zve10.c`) delivers interrupts — the ISR runs
  thousands of times — and the driver submits ~19 fence packets, but the ioctl never completes.
  Forcing the ioctl to return success aborts `Application_Start` at ~2,070 log lines.

**What would resolve it:** a faithful vendor graphics state machine, not another register guess.

#### 3. `/dev/streamfb` pixel format unconfirmed

Neither RGBA4444 (`R=v&0xf, G=(v>>4)&0xf, B=(v>>8)&0xf`) nor RGB565 produces a sane image.
**Any judgement about frame content from before this was noticed is unverified.**

### Assumptions already excluded

Do not re-test these — all were measured and failed:

- Removing `libInfraBluetoothLowEnergy.so` to skip BLE init. It works as a bypass, but BLE is on the
  **mandatory boot path by design** (the companion app connects over BLE at power-on), so it is not a
  porting path.
- `0x7a0020` as a "missing input queue" — it is missing on **both** generations; the registration table
  is a *callback* list, not a *queue* list. Non-bug.
- Excluding modules from `libIMDB.so` to emulate the older reference project. **Harmful here** — removing
  it restores the `infra_wrapper_bus_power` assertion path.
- Forcing the frame loop branch, faking the `0x910042` query, replacing `osal_uipc.ko`, loading 25 kernel
  modules, aligning reference-project parameters, `isFrameDelay` → 1.

### Corrections that matter for this work

- **`dmode=3p` does not enable `LD_PRELOAD` — it clears it.** `bootin.elf` at `0x9b24` tests bit 6 of the
  mode word and calls `putenv("LD_PRELOAD")` with no `=`, which removes the variable, immediately before
  `execve`. Any injection scheme relying on `preload` + `3p` cannot work.
- **`/usr` is plain uncompressed ext2 on `nflasha15`, and writes persist.** The LZP claim was wrong
  (upstream retracted it); see [`EXTERNAL_ZVE10_RE.md`](EXTERNAL_ZVE10_RE.md) §6.

### Where to read next

| Document | What it is |
|---|---|
| [`QEMU_M2_PROGRESS.md`](QEMU_M2_PROGRESS.md) | Full round-by-round ledger (~11.8k lines). Authoritative, including retractions. |
| [`MASTER_HANDOVER_NEWMENU.md`](MASTER_HANDOVER_NEWMENU.md) | 15 rounds of static analysis. **Read its conclusions against this page.** |
| [`QEMU_M2_EXPERIMENT_RESULT.md`](QEMU_M2_EXPERIMENT_RESULT.md) | First QEMU comparison, older vs newer stack (mid-way snapshot). |
| [`REALDEVICE_FROM_QEMU.md`](REALDEVICE_FROM_QEMU.md) | Three on-device checks and a decision tree. |
| [`M2_MENU_RESOURCES.md`](M2_MENU_RESOURCES.md) · [`m2-vs-e10-libs.md`](m2-vs-e10-libs.md) | Resource and library inventories. |

### Next steps

1. Determine whether the missing backup receiver is a registration-order problem (registrant starts later
   than `MWF_Init` waits) — the most promising single lead.
2. Decide the graphics question one way or the other: either implement enough of the vendor state machine
   to complete the ioctl, or abandon the driver and route rendering through a software path.
3. Nail down the `format=1` pixel layout, otherwise "did the UI draw?" is unanswerable even when it does.

---

## 简体中文

### 目标

把 **ZV-1M2 的菜单栈**（CXD90045_k8 代）搬到 **ZV-E10** 上。这不是改个设置的事：两代的 UI 引擎
结构不同。

| | ZV-E10（老） | ZV-1M2（新） |
|---|---|---|
| `libObj.so` | 21 MB，**导出** `Application_System_Init` | 3.9 MB，**不导出** |
| UI 形态 | 单层 | 两段：`appFwBoot`（8 KB）→ `appFw`（64 MB）→ `Application_Start` |
| 初始化入口 | `Application_System_Init(144 B)` | `uxengine_init2(216 B)` |

### 当前进度

以下均测于 QEMU，**没有日志行支撑的结论不写在这里**。

```
混合 /usr 树构建完成并可启动                        ✅
INITSEQ 解析条目：66                                ✅
进入 Application_Start                              ✅
提交图形包（GPE）：19                               ✅  （首次真实 GPU 活动）
从我们自己的进程抓到帧：0                            ❌
```

**没有帧。从来没有帧。** 早先那条"新菜单已渲染"的记录已于 2026-10-28 撤回：抓帧脚本用的是
`pgrep -x qemu-system-arm | head -1`，当时跑着两个模拟器，那一帧属于另一个会话的老菜单进程。
用启动时记录的确切 PID 重测，每次都是 0.0% 非零。当时一并报道的"1/6 间歇性渲染"也根本不存在。

### 三条阻塞

每条都钉到具体接口，而不是笼统的"卡在某处"。

#### 1. backup 服务队列 `0x3e014d` 无接收者

- `osal_valloc_msg_wait(0x3e014d, …)` **成功** —— 队列存在且能分配。
- 观察到的 **62** 条注册记录里没有任何收消息入口；`libbackup.so` 只导入了
  `osal_reg_msg_queue_cb` / `osal_unreg_msg_queue_cb`。
- `backup.ko` 在**插入时**必须 `file_load` 到合法的 `/setting/Backup.bin`；固件包不自带
  （`0800_appli/setting` 只有 `DmmConfig.bin`、`facedmy.bin`、`kemco.txt`、`ulogio.bin`、
  `appfw_infra`），说明它是首启生成的。
- `Backup_load=0` ⇒ `insmod` 直接失败（`Input/output error`，status 5），并连带 `ulogio`、`dmm`、
  `udate` 因 `Backup_read*` 解析不到而一起挂掉。
- 伪造回包（`backup_sync_msg` 返回成功、`*out=0` 或 `*out=1`）会让启动卡在 `MWF_Init`（约 452 行）。
- 试过三种 `Backup.bin`（固件默认 / 真机生成 / 手工改版本串）—— 都能加载，都不解锁。

**解法方向：** 拿到真正首启生成的 `/setting/Backup.bin`，或让注册者启动，或搞清为什么没人注册这个队列。

#### 2. 图形设备模型不完整

- `grm_gles.ko` 在 **IRQ 76** 请求线程化中断（`IRQF_DISABLED|IRQF_TRIGGER_HIGH`，`0x84`）。
- `sugilite_interrupt` 在 `bar + 0x34` 读状态；**读到 0 会触发内核 BUG**。
- 状态位：`0x80000000` → 清 `bar+0x1000`；`0x40000000` → 清 `bar+0xC18`；
  `0x04000000` → 清 `bar+0x1C`；低 4 位 → 向 `bar+0x3C` 和 `bar+0x44` 写 1。
- 引擎等待的完成位是**第 26 位**（`0x04000000`）。
- 我们的设备模型（`/mnt/build/m2qemu/qsrc/hw/arm/zve10.c`）能送中断（ISR 跑了数千次），驱动也提交了
  约 19 个栅栏包，但 ioctl 永不完成。强行让它返回成功会在约 2,070 行处 abort `Application_Start`。

**解法方向：** 一个忠于原厂的图形状态机，而不是再猜一个寄存器。

#### 3. `/dev/streamfb` 像素格式未确认

RGBA4444（`R=v&0xf, G=(v>>4)&0xf, B=(v>>8)&0xf`）和 RGB565 都得不到合理图像。
**在这点被注意到之前，所有关于帧内容的判断都未经证实。**

### 已排除的假设

以下都实测失败，不要重测：

- 移除 `libInfraBluetoothLowEnergy.so` 跳过 BLE 初始化。它确实能作为绕过手段，但 BLE 在**设计上位于
  启动必经路径**（手机 App 开机即通过 BLE 连接），因此不是移植方案。
- 把 `0x7a0020` 当成"缺失的输入队列" —— **两代都不注册它**；注册表是**回调**清单，不是**队列**清单。伪 bug。
- 从 `libIMDB.so` 里排除模块来模仿老参考项目。**这里是有害的** —— 恢复原版才消除
  `infra_wrapper_bus_power` 断言。
- 强制帧循环分支、伪造 `0x910042` 查询、换 `osal_uipc.ko`、加载 25 个内核模块、对齐参考项目参数、
  `isFrameDelay` 强制为 1。

### 对本工作有影响的更正

- **`dmode=3p` 不是启用 `LD_PRELOAD`，而是清除它。** `bootin.elf` 在 `0x9b24` 测模式字的第 6 位，
  若置位则调用不带 `=` 的 `putenv("LD_PRELOAD")` 删除该变量，紧接着才 `execve`。任何依赖
  `preload` + `3p` 的注入都不可能生效。
- **`/usr` 在 `nflasha15` 上是普通未压缩 ext2，写入是持久的。** "LZP 压缩"的说法是错的（上游已撤回），
  见 [`EXTERNAL_ZVE10_RE.md`](EXTERNAL_ZVE10_RE.md) §6。

### 接下来读什么

| 文档 | 是什么 |
|---|---|
| [`QEMU_M2_PROGRESS.md`](QEMU_M2_PROGRESS.md) | 逐轮完整账本（约 1.18 万行）。权威，含撤回记录。 |
| [`MASTER_HANDOVER_NEWMENU.md`](MASTER_HANDOVER_NEWMENU.md) | 15 轮静态分析。**其结论请对照本页阅读。** |
| [`QEMU_M2_EXPERIMENT_RESULT.md`](QEMU_M2_EXPERIMENT_RESULT.md) | 首次 QEMU 对照，新老栈（中途快照）。 |
| [`REALDEVICE_FROM_QEMU.md`](REALDEVICE_FROM_QEMU.md) | 真机三条判据与决策树。 |
| [`M2_MENU_RESOURCES.md`](M2_MENU_RESOURCES.md) · [`m2-vs-e10-libs.md`](m2-vs-e10-libs.md) | 资源与库清单。 |

### 下一步

1. 判断 backup 接收者缺失是否为**注册顺序**问题（注册者晚于 `MWF_Init` 的等待）—— 最有希望的单一线索。
2. 图形问题必须做个决断：要么把原厂状态机补到能完成 ioctl，要么放弃该驱动改走软件渲染路径。
3. 钉死 `format=1` 的像素布局，否则"UI 到底画了没有"这个问题永远无法回答。

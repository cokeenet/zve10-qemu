# ZV-1M2 Gen5 菜单在 ZV-E10 QEMU 中的最终结论（round 288）

> 本文只记录 QEMU 侧（`-M zve10` 机器模型）的实测结论，全部数字来自 `/mnt/build/m2qemu/out/v*/`。
> 真机路线见 `MASTER_HANDOVER_NEWMENU.md`。
> **已剔除早期被"测量污染"的结论**（见第九节），只保留经多次交叉验证的事实。

---

## 一、一句话结论

**Gen5 固件在 QEMU 里启动链完整跑通、UI layer 树完整建立、绘制 API 可调用，
但它在启动完成后再也不产生任何绘制工作 —— A9 侧与 GPU 侧都静止。**

**这不是"缺某个参数"，而是固件的绘制路径在当前环境下没有可执行的内容。**

---

## 二、已确证的事实（每条都有证据来源）

| # | 事实 | 证据 | 交叉验证 |
|---|---|---|---|
| 1 | 启动链全通：bootin → appFwBoot → im.elf → appFw | sha256 匹配、2730 构造函数 | 多轮 |
| 2 | 14 个内核模块全部加载成功 | `MODULE_END ... status=0` | 多轮 |
| 3 | `createLayer` 调用 **6 次，全部返回非 NULL** | `CREATELAYER n=1..6` | v303/v310/v312/v314/v323 |
| 4 | layer 3 使用固件自己生成的 ID（`0x8001653f`） | `CREATELAYER n=3` + GENIDPROBE AAA | v303 |
| 5 | `uxengine_init2` 执行并返回非 0，且首行设 640×480 | `INIT2PROBE return r≠0` + 反汇编 | 多轮 |
| 6 | `GFXENG` 被进入并返回 | `GFXENG enter n=5 → leave ret≠0` | v303 |
| 7 | **WidgetSystem 4 个绘制 API 全部可调用** | `DRIVE_ORDER` × 3 + `notifyDraw` 返回非 0 | **v320** |
| 8 | 输入注入 API 齐备 | 导出 `postMousePress/Release/Move` | 符号表 |
| 9 | `updateFrame` 真实遍历 **5 个 layer 对象** | `UPDATEWATCH 105f98_panel hit=1..5` | v320 |
| 10 | 遍历对象的 `+4` 正是 createLayer 返回的 layer id | AAA–DDD 一一对应 | v320 |
| 11 | 画布已映射，大小恰好 614400 字节 | `MMAPOBS off=0x22d52000 len=0x96000` | 多次 |
| 12 | 画布读数恒为零（6 次采样，运行时跟踪 VA） | `CANVASX/CANVASY nz_words_512=0` | v296 |
| 13 | `updateFrame` 的 PLT 槽 **never-called**（无驱动时） | `GOTS ... PLT0(never-called)` | v303 |
| 14 | `isFrameDelay` / `updateCurrentTime` 探针 0 命中（无驱动时） | 无污染基线 | v303/v311 |
| 15 | **GLES 设备 303 次 ioctl 全部成功** | `GLESIOCTL ... ret=0`（303/303） | **v323** |
| 16 | **GPU 设备全程只被访问 75 次，全在开机最初 134 行** | `qemu.err` 30564 行里 PCSET=30128、GPE=75 | **v323** |
| 17 | GPE 只有 19 个 init fence，握手值恒为 `12345678` | 无任何绘制包 | v303/v322/v323 |
| 18 | **layer 绘制虚槽 `libObj+0xdeaf4` 是 3 指令空壳，真机原版亦然** | 逐字节对照 | **第 698 章** |
| 19 | **8 个 `GRM_bitmap*` 接口全部 0 命中（无 bitmap 对象）** | 无 bitmap 创建 | v323 |
| 20 | 5 个 framebuffer 接口（`GRM_fbrm*` 等）全部 0 命中 | 无 fps 交互 | 多轮 |
| 21 | 数据代对齐有效：换成 ZV-1M2 自己的备份后，关键读取从 -1 变 +1 | `BACKUP_nread key=0x2cf1844 ret=1` | **v317/v318** |
| 22 | 但数据代对齐**不足以**进入绘制 | v318 仍零绘制 | v318 |

**⇒ 第 7/9/10 条证明"驱动可行"，第 15/16 条证明"GPU 侧不是瓶颈"，
第 18/19 条证明"A9 绘制路径在设计上就是空的"。**

---

## 三、绘制链的完整结构（反汇编证据）

```
appFw 0x54ed12  主循环函数入口
  0x54ed22  blx  WidgetSystem::updateCurrentTime()
  0x54ed36  blx  WidgetSystem::processEvent()
  0x54ed50  blx  WidgetSystem::isFrameDelay()
  0x54ed54  cbz  r0, 0x54ed90          ← 为 0 就跳出
  0x54ed5a  blx  WidgetSystem::updateFrame()

appFw 0x54faae  bl 0x54ed12            ← 全 appFw 唯一的循环调用点
  0x54fa3c  bl  0x54f2ca
  0x54fa40  cmp r0, #2
  0x54fa44  beq 0x54faf0               ← 返回 2 就跳过

libObj 0x106572  updateFrame
  0x1065c2  ldr  r3,[r4]               ← layer 的 vtable
  0x1065c6  ldr  r3,[r3]               ← slot 0（"要不要画"）
  0x1065c8  blx  r3
  0x1065ca  cbz  r0, next              ← 0 就跳过
  0x1065d0  bl   0x105f98              ← 绘制入口
              0x105fc4  ldr r3,[r3,#0x24] ; blx r3
              0x106014  ldr r3,[r3,#0x28] ; blx r3

slot24 实测 = libObj + 0xdeaf4
  0xdeaf4: push {r7,lr} ; adds r0,#24 ; add r7,sp,#0 ; pop {r7,pc}
  ⇒ 3 条有效指令的空壳（真机原版逐字节相同）
```

---

## 四、为什么"再往下就观测不到"（两条硬约束）

```
① 0x54f2ca 不是导出符号（落在 Application_Start 内部），
   且其所在位置 4+2+4 = 10 字节窗口，8 字节 trampoline 会切断指令
   ⇒ 无法 hook

② 本轮又确认第四个禁区：
   ✗ osal_reg_sync_msg_queue_cb 被覆盖 ⇒ 启动崩
   ✗ IMDB_find_entry 被覆盖 ⇒ 启动崩
   ✗ INITWRAP 用 4 参 wrapper 替换无参函数 ⇒ 启动崩
   ✗ CBWRAP 替换注册回调指针 ⇒ 启动崩（349 行）
   ⇒ **启动早期被解析/注册的任何东西都不能替换**
```

---

## 四之二、三个"强证据的 0 命中"（第 705~711 章的最终测量）

**这三个计数器的命中都会立即打印，因此"0"不受日志窗口边界影响。**

| 探针 | 目标 | 语义 | 实测 |
|---|---|---|---|
| `GATEWATCH` | appFw+0x54f2ca | 主循环的唯一判据函数 | **0**（钩子 installed wrote=10） |
| `UPDATEWATCH` | libObj+0x105f98 | layer 绘制入口（纯观测基线） | **0** |
| `FILLWATCH` | libObj+0x1127f8 | **全库唯一**调用 `GRM_gpermFillrect@plt` 的函数 | **0**（钩子 installed wrote=8） |

**三者合并的结论：**

```text
从"主循环门"到"真光栅入口"，整条链上每一个可插桩的点都是 0。
⇒ 固件从未走到绘制 —— 不是某一个环节坏了，而是**根本没有开始**。
```

**因果链自洽：**

```text
0x54f2ca 从未调用 ⇒ 主循环 0x54ed12 从未运行
  ⇒ updateFrame 从未执行 ⇒ 0x105f98 从未执行
  ⇒ 真光栅入口 0x1127f8 从未执行（它也只有一个 PLT 路径，无直接 BL 调用者）
```

**而光栅实现本身是真的：**

```text
GRM_gpermFillrect（0x117b4c）有 300+ 字节：
  · 参数解析、矩形裁剪（bl GRM_bitmapGetSubRectangle）
  · **tbb [pc,r3] 格式跳转表（bitmap 格式 1..8 八路分发）**
  · 提交调用链 0x115968 → 0x115364
⇒ 它**不是空壳**；真正是空壳的只有 layer 的绘制虚槽（libObj+0xdeaf4，真机原版亦然）

⇒ 因此问题是"**调用链在早期就断了**"，不是"没有绘制实现"。
```

---

## 五、已排除的假设（每条都有测量依据）

| 假设 | 如何排除 |
|---|---|
| 缺 `/dev` 设备 | `/dev/mem` 打开成功 |
| mmap 失败 | 29 次 mmap 0 次失败 |
| 参数不对（108 组） | 全返回 -9999，且该路径固件根本不走 |
| 我们注入造成污染 | 关注入后行为不变 |
| `dmm.ko` 是队列接收方 | dmm 用的字面量是 `0x8f013a` |
| 是"硬件状态轮询" | 映射者是 dmp 分配器（普通内存分配） |
| 缺未加载的 Infra 库 | 引用库都在部署树里 |
| 需要第二个 CPU 核 | Gen5 无双核负载；`-smp 2` 已知必 panic |
| 缺 `dma330_stub` | 加载后 liro 能起（但会饿死主流程） |
| `dma330` 真驱动导致挂起 | 用 stub 同样挂起 |
| `[self+0x870]` 是忙计数 | 两次运行值不同且是可打印 ASCII 残留 |
| 缺软件渲染后端 | 固件连绘制请求都没发出 |
| **GLES 同步阻塞** | **v323：303 次 ioctl 全部 ret=0** |
| **GPE 包完成与否是瓶颈** | **v322：完成所有包仍无变化** |
| **a7c2 的 Dataflow pin 方案** | `0x9080003b` 是字节搜索误报（跨两条指令） |
| **a7m5 的 libgpe 方案** | 我们树里没有 `libgpe.so` |
| **"首设已完成"假设** | 换干净模板（v316）与换同代模板（v318）行为均不变 |
| **备份数据代不符** | 已修复（会让读取成功）但不导致绘制 |

---

## 六、`av-cam.bin` / MUSASHI 的单变量对照

```
liro.ko = uITRON RTOS + MUSASHI 固件加载器
  · 加载 /system/av-cam.bin（17,195,164 B）到 0x635c6000
  · 创建 2 个内核线程，RTOS 任务优先级 5
  · 固件确实执行（737 个 PC 样本落在 0x635c6000..0x6462c69c，全唯一地址）

单变量对照：
  av-cam.bin 存在  → t=120 停
  av-cam.bin 移除  → t=1125 健康、createLayer=6
⇒ **在单核上，MUSASHI 主循环与 UI 主流程互斥。**
```

**补充：参考树 `ref-chocotan-zve10/.../system/` 是空目录 ⇒ 它根本没有 MUSASHI 固件可加载。**
**而参考的帧来自 Gen4 自己的软件渲染路径，与 MUSASHI 无关。**

---

## 七、参考实现的可行性分析（三个仓库全部读过）

| 仓库 | 路线 | 对本目标 |
|---|---|---|
| `sonyfw-zve10-qemu` | 软件渲染后端 + present 捕获（Gen4 自己的固件） | ✗ 它跑的是 Gen4，不是 Gen5 |
| `a7c2-qemu` | AArch64 `virt+cortex-a35` + 软件 GPE 队列 | ✗ ISA 不同、依赖 `libgpe`（我们无） |
| `a7m5-qemu` | 同代（Gen5/AArch64），**成功**，靠 `libgpe` 5 个 PLT 槽 | ✗ 我们树里没有 `libgpe.so` |

**⇒ 三个参考都依赖 ZV-1M2 没有的组件或不同 ISA。**

---

## 七之二、卡点的最终精确坐标（本章为第 700~707 章的收敛结果）

**整条链每一环都有地址证据，只有最后一环受观测窗口限制。**

```
Application_Start（appFw 0x54d580，唯一导出符号）
  ├─ uxengine_init2             ✓ 返回非 0
  ├─ initializeArena / setDatabase / setFontName   ✓ 都返回
  └─ render container（0x54f664）
       ├─ RENDERENTRY enter=1                      ✓ 进入
       ├─ 0x54f674  bl stage1（0x54ef74）
       │    └─ STAGE1TEL=1                          ✓ 进入
       │    └─ 0x54eff8  pop {r4,r5,r6,r7,pc}       ✓ 有返回指令（代码存在）
       ├─ 0x54f68a  bl 0x54833c                     （约 20 条指令的函数，有 pop）
       ├─ ...（约 20 个 bl/blx）
       ├─ 0x54f9e8  blx 0x925674   ← **门之前最后一个外部调用**
       ├─ 0x54f9ee  b 0x54fa3a
       │    └─ 0x54fa3c  bl 0x54f2ca（门函数）
       │         └─ **GATEWATCH 实测：整个运行 0 次命中**
       │              （钩子 installed wrote=10，命中会立即打印，不受窗口边界影响）
       ├─ 0x54fa40  cmp r0,#2 ; 0x54fa44 beq 0x54faf0   ← 返回 2 则跳过主循环
       ├─ 0x54faa4  cmp.w r8,#0 ; 0x54faa8 bne 0x54facc ← 返回 0 则不跳
       └─ 0x54faae  bl 0x54ed12（**全 appFw 唯一的主循环调用点**）
            └─ updateCurrentTime → processEvent → isFrameDelay → updateFrame
                 └─ 0x106572 updateFrame：逐 layer 取 vtable slot0
                      └─ 非 0 则 0x105f98（进入绘制）
                           └─ vtable+0x24 → **libObj+0xdeaf4（3 指令空壳，真机亦然）**
```

**门的判据（0x54f2ca 内部 0x54f2fa）：**

```
ldr.w r0, [r5, #0x870]     ← self+0x870
cmp   r0, #2
movgt r0, #2               ← >2  ⇒ 返回 2  ⇒ 跳过主循环
movle r0, #0               ← <=2 ⇒ 返回 0  ⇒ 主循环运行
```

**结论：**
```text
✓ 强证据：门函数 0x54f2ca 从未被调用（GATEWATCH 0 命中，钩子确实装上）
✓ 反汇编：门之前最后一个外部调用是 0x54f9e8 的 blx 0x925674
? 未证：0x925674 是否返回（主线程末条记录距日志末尾仅 7 行，
        与第 662/676 章同一窗口陷阱，不能断言"从未返回"）

⇒ 最可能的阻塞点：**0x925674（appFw 内部的一个 veneer/抽象调用）**
⇒ 若它不返回，则门从不被调用，主循环从不运行，updateFrame 从不执行，
  此后即使 updateFrame 执行，libObj+0xdeaf4 也是空壳 ⇒ **两个独立原因都在**
```

---

## 八、结论：在什么条件下这个目标可以实现

```
① 修改固件：把 layer 绘制虚槽（libObj+0xdeaf4）等空壳替换成真光栅
   ⇒ 需要重编译 libObj（我们没有源码，且会破坏 sha 一致性）

② 或用固件的 bitmap API 自建画布并写入
   ⇒ 会滑向"自绘"（判据 1 不允许），除非几何/颜色来自固件意图

③ 或让 MUSASHI 不与 UI 主流程互斥（需多核，而 -smp 2 已知 panic）

④ 真机验证（不依赖 QEMU）—— 唯一可能真正看到新菜单的路
```

---

## 九、测量污染的教训（本会话最大的方法收获）

| 曾得出的（错误）结论 | 推翻方式 |
|---|---|
| "绘制函数被真正调用过（draw_entry 10 次）" | 那是我自己 DRIVE_NOTIFY=1 触发的 |
| "GRM_bitmapCreate 被调用 24 次" | 全在 tid=195，是我的 GCREATE 探针自调用 |
| "notifyDraw 从未返回 ⇒ 卡死在 OSAL" | 窗口在主线程停后仅差 6 行到期 ⇒ 窗口伪影 |
| "INITSEQ 走到第 62 项后停住" | INITSEQ 只是 dlsym 名称解析 |
| "`[self+0x870]` 是 >2 的忙计数" | 两次运行值不同，是可打印 ASCII 残留 |
| "stage1 转发到渲染容器" | 该指针解析后落在 `libUsbBusPower.so` |
| "sugilite_ioctl 卡住 ⇒ GLES 阻塞" | 303 次 ioctl 全部 ret=0（纯轮询） |
| "50 个回调没被执行" | PC 采样太稀疏，不足以证明（弱证据，已标注） |

**纪律（建议沿用）：**
```
· "没有观察到" ≠ "不存在" —— 先证明测量能看到非零
· 任何"从未/为零"的结论，都要检查窗口边界与采样密度
· 任何单次读数，都要重跑一次确认可复现
· 加观测手段前，先确认 ABI/语义与被观测函数完全一致
· 自己发起的调用必须与固件自身行为严格区分（按 tid/caller 过滤）
· 搜索命中必须反汇编复核（字节可能跨指令）
```

---

## 十、状态与可复现命令

```
QEMU 实例   0
最新基线    v328: 5501 行 / createLayer=6 / LIBOBJ_SYM=24
可用基线    v303 / v310 / v312 / v314 / v318 / v322 / v323 / v325 / v327 / v328
账本        /root/fw/oss/docs/QEMU_M2_PROGRESS.md（711 章）
```

复现干净基线：
```bash
cd /mnt/build/m2qemu
# init 第 44 行：FORCE_MENU=0 DRIVE_NOTIFY=0 NO_DRIVE=1
#               INJECT_INPUT_QUEUES=0 INJECT_CP=0 FORCEDRAW=0
WINDOW=600 ./run-wlist.sh
```

**本会话未达成目标（一个像素都没画出来）。但 711 章的实测把"为什么"从
"不知道"推进到了"两侧都静止"，并逐条排除了 20 个假设。这份边界描述即本文。**

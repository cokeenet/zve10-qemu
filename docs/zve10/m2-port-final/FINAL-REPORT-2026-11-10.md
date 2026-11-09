# ZV-1M2 Gen5 新菜单移植到 ZV-E10：最终技术报告

2026-11-10。目标轮 1–101（预算 120）。**目标未达成：从未取得任何由 Gen5 固件绘制的帧。**
本报告只记录实测到的内容，不含推测性结论。

---

## 一、已完成的移植工作

### 1.1 库对齐（此前完全缺失的一步）

原始移植树 `PORTED4` 只换了 3 个库，其余沿用 ZV-E10 原厂版。逐文件哈希比对四个机型后
发现 **71 个库内容不同**，其中最关键的是：

```text
libInfraSKD.so      ZV-E10 10,184 B   →  ZV1M2 7,259,708 B   （差 700 倍）
libSysDef.so        ZV-E10 3,267,652  →  ZV1M2 6,033,848
libInfraMediaCommon.so  331,236       →  422,712
libsencore.so           366,412       →  375,484
libUsbControl.so         94,388       →   98,356
```

来源判定（按 SHA256 与四机型树逐一比对）：

```text
ZV1M2, ZVE10, ZV1F  98 个（三机通用）
ZVE10               94 个（原本是原厂版，已全部换成 ZV1M2）
ZV1M2, ZV1F         11
ZV1M2               10 个（原本就是 M2 的）
ZVE10, ZVF1F         1
```

替换后 `PORTED4/usr/lib` 与 `ZV1M2/usr/lib` **完全相同（190 个文件，0 差异，0 缺失）**。
`libObj.so`(4cd654f1) 与 `appFw.so`(f08c9eb7) 未被改动。

`bootin.elf` 与 `im.elf` 也已从 ZVE10 版换成 ZV1M2 版
（`bootin 3eff4684…` / `im 9d6da003…`），依赖闭包全部满足。

### 1.2 一个确证的故障被消除

换库前，`Application_Start` 期间触发：

```text
AS fn=void lib::assert::notify_model_system_error()
AS a=false f=lib_assert.cpp l=30
ASSERTBASE base=… name=/usr/lib/appFw.so
```

换库（71 个）后该断言 **完全消失**。这是本轮唯一确证的修复。

---

## 二、渲染链完整结构（静态解出 + 实测印证）

所有地址以实测基址为准（见第四节）。

```text
Application_Start            appFw+0x54d580   （dynsym size = 72，仅 6 个调用）
  54d590 blx A
  54d59a bl  54d154
  54d5a6 bl  54d2b0 (C)        ← 实测 enter=1 / leave=1，正常返回
  54d5ac bl  54f664 (容器)      ← 实测 enter=1
  54d5b0 blx D
  54d5b6 bl  E

容器 appFw+0x54f664 —— 消息驱动主循环
  54f674 bl  54ef74            ← stage1，实测 1 次
  ……
  54f6e2 bl  54e8f0            ← 实测 enter=0（从未进入）
  54f6fe b.n 0x54fb3e
  54fb3e blx gfxeng()
  54fb42 blx GraphicsEngine::processSystemMessage()
  54fb46 cmp  r0,#0
  54fb48 bne.w 0x54f700        ← 有消息才回渲染路径
  54fb5e ldmia.w sp!,{…pc}     ← 否则容器返回
  54f700 ……
  54f70e cbz  r5, 0x54f752     ← 第二道门（r5 来自栈参数 [r7,#44]）
  54f716 bl  0x712100
  54f71a bl  0x60e2f4
  54f78a bl  0x54f548          ← 渲染迭代，实测 0 次

渲染迭代 appFw+0x54f548
  54f560 ldr r4 → 全局字 appFw+0x37e39f0
  54f566 cmp  r3,#7            ← 实测值为 0x7，条件满足
  54f580 bl   0x54f468         ← 实测 0 次
  ……
  54fa72 bcs  0x54fad4         ← 帧节流，常量 0x62F5 = 25333
  54faae bl   0x54ed12         ← 渲染一帧

渲染一帧 appFw+0x54ed12
  54ed22 blx WidgetSystem::updateCurrentTime()
  54ed36 blx WidgetSystem::processEvent()
  54ed74 blx notifyDraw@plt        ← 全库唯一调用点
  54ed82 blx drawFramebuffer@plt   ← 全库唯一调用点
```

**静态证实：`.rel.plt` 与反汇编双重确认 `notifyDraw`（libObj+0x106524）与
`drawFramebuffer`（libObj+0xe565a）在 appFw 中各只有 1 个调用点，都在 `0x54ed12` 内。**
因此只要 `0x54ed12` 不执行，就不可能有任何绘制像素。

---

## 三、逐层实测（每层独立 inline hook，均校验 prologue）

所有 hook 都用同一套机制：`memcmp` 严格校验入口字节 → 复制进匿名 trampoline →
`mprotect` RWX → 改写入口为间接跳转 → `clear_cache` → 恢复 RX。

| 目标 | 地址 | 可搬长度 | 实测 |
|---|---|---|---|
| `WidgetSystem::notifyDraw` | `libObj+0x106524` | 12B | **0**（禁用自调用后） |
| `GraphicsEngine::drawFramebuffer` | `libObj+0xe565a` | 12B | **0** |
| `Lz77Decoder::decodeImpl` | `libObj+0x10bff8` | 10B | **0** |
| `GRM_gpermFillrect` | `libObj+0x117b4c` | 12B | **0** |
| `GRM_gpermRectblit` | `libObj+0x117c86` | 12B | **0** |
| `GraphicsEngine::processSystemMessage` | `libObj+0xe5610` | 8B | **1 次，返回 1** |
| 容器入口 | `appFw+0x54f664` | 10B | **enter=1** |
| 容器 stage1 (`0x54ef74`) | `appFw+0x54ef74` | 6B | **1 次** |
| 容器 stage2 (`0x712100`) | `appFw+0x712100` | 6B | **0** |
| 容器 stage3 (`0x60e2f4`) | `appFw+0x60e2f4` | 4B | **0** |
| 渲染迭代 | `appFw+0x54f548` | 12B | **0** |
| 迭代下一层 | `appFw+0x54f468` | 10B | **0** |
| 容器 `0x54f6e2` 的目标 | `appFw+0x54e8f0` | 8B | **enter=0** |

**结论：容器进入并执行了第一个调用（stage1），其后全部为零。**

---

## 四、方法层面的一次重要修正：库基址

前 50 轮里我用手工推算的库基址做地址换算，**连续三次算错**：

```text
错误做法：base = (某运行时地址) − (某文件内偏移)
实测（R55 起由探针读 /proc/self/maps）：

/usr/lib/libObj.so   base=0xa954000    （手工推算得 0xa89a000，差 0x6A000）
/usr/lib/appFw.so    base=0x2dbf2000   （手工推算得 0x2e0eb000）
```

原因：`libObj` 的 `PT_LOAD p_vaddr` 不为 0，而 `appFw` 有两个 `PT_LOAD` 段
（`vaddr=0x0` RX 与 `vaddr=0x3447000` RW），**装载基址 = 第一段运行时地址 − 第一段 vaddr**，
不能拿任意段相加。

**R55 起改为由探针打印 64 个模块的 maps 基址（`BASEPRINT`），此后所有换算以此为准。**

同类的第二处错误：`0x54f560` 的 `add r4,pc`，Thumb 的 PC 值是「指令地址 + 4」，
我最初用指令地址导致全局变量偏移算成 `0x37e39ea`（错 6 字节），
正确值是 `0x37e39f0`。**改对后实测该全局字为 `0x7`**——此前基于错偏移得出的
「全局状态不是 7、门关着」的判断完全错误。

---

## 五、唯一自洽的解释（基于实测，非推测）

```text
Application_Start 进入               ✅ 日志有 ENTER
B (0x54d154) / C (0x54d2b0)          ✅ 实测正常返回
容器 0x54f664 进入                    ✅ enter=1
容器 stage1 (0x54ef74)               ✅ 1 次
容器在 0x54ef74 之后、0x54f6fe 之前停滞 ❌ 其间 5 个 PLT 调用 + 0x54e8f0 全为 0
                                     （0x54e8f0 实测 enter=0）
之后所有绘制入口                       ❌ 全 0
CPU                                  ✅ 38% 用户态执行，其余 WFI 空闲
PC 采样（2479 样本）                   ❌ 0x54dxxx / 0x54exxx / 0x54fxxx 全部零命中
线程 195 最终停在                     qid=0x1cf60b9 主泵，tmo=-1（无限等待）
该队列被发送过                         ❌ 全日志仅 1 条记录（就是那次等待）
```

---

## 六、已实测排除的假设（13 条）

| # | 假设 | 排除依据 |
|---|---|---|
| 1 | 缺 Gen5 依赖库 | 成立（71 库替换后断言消失）；但替换后仍未出帧 |
| 2 | 缺按键 | 20 字节 InfraInput 按键 `send=0` 投递成功，无任何变化 |
| 3 | 启动卡死 | PC 采样 `cpu_v7_do_idle` 占主导，CPU 在 WFI 空闲 |
| 4 | 子系统 13 无应答 | 换库后 `MODEL_SYSTEM_ERROR` 断言消失 |
| 5 | `0x2b2223e` 是主泵处理器 | 反汇编仅 `ldr r0,[r0,#-4]; pop` —— 4 字节访问器 |
| 6 | `switchBank` 阻塞 | 24 条 RM 记录，ENTER/LEAVE 成对，`ret=1` |
| 7 | 调 `0x723aac` 可驱动菜单 | 源码注释记载 ledger 180 已死锁，未采用 |
| 8 | `0x8f013a` 缺应答者 | 62 个库共用该常量，是日志队列 |
| 9 | 容器不返回 = 卡死 | 容器是主循环；`sleep(5)` 在本 guest 不生效，报告线程本就不活跃 |
| 10 | 全局状态≠7，门关着 | 修正偏移后实测 `u32=0x7` |
| 11 | 探针重入导致阻塞 | 禁用 `DRIVE_AT_SEND` 主动调用后行为不变 |
| 12 | 缺 `ldec.ko` 可加载硬件解码 | 加载后 `createLayer` 归零，基线崩塌，已回退 |
| 13 | GOT 槽 `PLT0(never-called)` 可证未调用 | libObj 内部调用本就不过 PLT，该指标无证明力 |

---

## 七、为什么最终出不了帧（诚实分析）

### 7.1 不是缺件，是「没人驱动」

渲染链的每一层都被实测证明是「可达但未被执行」，而不是「执行中失败」。
容器跑完第一圈后，线程就离开容器区（PC 采样零命中佐证）并回到主泵无限等待。

### 7.2 真正的障碍是探针基础设施，不是固件

最后三次「主动驱动」尝试全部因同一原因失败：

```text
尝试 1（R45）  在 appFw+0x54ed12 挂热路径 hook  → 行数 299 / AS 29 / createLayer 0，基线崩塌
尝试 2（R98）  独立线程 sleep(25) 后调 drawFramebuffer → 25 秒内运行已结束，从未执行
尝试 3（R99）  改为在 start_watchdog 块内调用 → 该块在本次运行中未执行，从未执行
```

根因：**这个 guest 的线程/定时机制在可观测窗口内不可靠。**
源码注释早已记载：

```c
sleep(5);   /* NOTE: does not elapse in this guest (ledger 194);
             * the monotonic busy-wait replacement starved startup,
             * so this probe is effectively inactive. */
```

**任何依赖「等一段时间后自动执行」的探针都失败。** 而「在确定执行的调用点里同步调用」又会
改变固件自身的调用时序（尝试 1 与 0x54f664 容器热路径都印证了这一点）。

### 7.3 与 a7c2-re 的差别在哪

a7c2-re 能在 QEMU 里出帧，方式**不是**「等固件自己循环」，而是：

> 显式软件 GPE 模型实际执行已核准的绘图请求；缺失的 LZ77 硬件解码由有边界检查的
> 同格式 CPU 解码替代。原厂生命周期入口只用来创建菜单，绘制由实验侧驱动。

即：**他们不等待固件，而是把绘制请求在 QEMU 侧真实执行掉。**

本任务走的是「让 Gen5 固件自然跑起来」这条路线，它在当前 QEMU 模型下的阻塞点是
「主泵无生产者 + 探针基础设施不可靠」，而不是某个可以修补的库缺失或断言。

---

## 八、可复现的资产

```text
运行脚本       /mnt/build/m2qemu/run-wlist.sh        （单实例 systemd-run + 收尾清扫）
探针源码       /mnt/build/m2qemu/src/argtrace.c      （LD_PRELOAD 观察器，含本报告全部 hook）
内核树         /mnt/build/m2qemu/work/kbuild-ui2
initramfs      /mnt/build/m2qemu/work/initramfs-ui   （1031 文件 / 265,951,776 B）
移植树         /mnt/build/m2qemu/port/PORTED4
ZV1M2 原始树    /mnt/build/fw/COMPARE/extracted/ZV1M2 （257 MB，只读参考）
回滚备份       /mnt/build/m2qemu/backup-lib-0409     （usr/lib 完整副本）
```

基线哨兵（每次运行都会记录）：

```text
行数 ≈ 1483 / CREATELAYER n=6 / LIBOBJ_SYM 24
```

本轮全部运行均满足 `createLayer=6`；报废轮次（R38、R45）已标注并从源码删除探针。

---

## 九、结论

```text
移植库对齐              ✅ 完成（71 库 + bootin/im，与 ZV1M2 完全一致）
MODEL_SYSTEM_ERROR      ✅ 已消除
渲染链结构              ✅ 完整解出（含每个地址与可搬字节数）
逐层测量                ✅ 全部可信（13 层独立 hook，prologue 校验）
合格 640×480 帧          ❌ 无
SHA256 / 物理地址        ❌ 无
```

**未达成目标。阻塞点是「固件主泵无生产者 + 本 guest 探针基础设施不可靠」，
不是可以靠继续增加 hook 解决的层次。**

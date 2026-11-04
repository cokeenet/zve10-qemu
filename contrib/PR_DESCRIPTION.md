来自 ZV-1F 新菜单移植项目的独立验证团队。我们独立走通了装载器全链路，App 稳定运行 150 秒零崩溃 —— 但**主线卡在了 uipc 应答缺失上**。我们没有放弃，仍在间歇性推进（`SKIPDMP` 绕过、探针 v2 等）。

## ⚠️ 2026-11-04 勘误（请先读）

上游在 `a87022c` / `docs/issues/REPLY_PR3_2026-11-04.md` 中撤回了两条本 PR 曾引用的结论：

| 原表述 | 更正 |
|---|---|
| 「上游移除 BLE 库后首次在 QEMU 渲染出新菜单」 | ❌ **撤回**。抓帧脚本 `pgrep -x qemu-system-arm \| head -1` 取到了**另一个并发 QEMU 进程**的画面；改用 `MYPID=$!` 重测后 M2 为 **0.0% 非零**。「1/6 间歇性渲染」同样不存在 |
| 「找到输入队列 `0x007a0020`」 | ❌ **撤回**。该地址**两代机型都不存在**，注册表是回调注册表而非队列表，不构成缺陷 |
| 「BLE/uipc 阻塞是共性」 | ❌ 删除。上游明确：移除 BLE 是 **workaround**（破坏 Creator's App 连接），不是移植路径 |

另有两处是我们自己 10-28 的证据审计推翻的，一并更正：

| 原表述 | 更正 |
|---|---|
| 「2425 次请求只等到 72 次应答（3%）」 | ❌ 右删失统计错误。72 = 35 成功 + 3 错误 + **34 条截至记录结束未返回**（不是永不返回）；涉及多个 qid，不能称 3% 应答率 |
| 「`13A` 请求 / `139` 应答配对」 | ❌ 两者都是日志服务的监听队列，配对不成立 |

**本次新增贡献**：`contrib/gpe-ioctl-package/` —— 上游索要的 `grm_gles.ko` 全 17 条 ioctl 注解表
与 GPE 设备模型补丁（含可直接合入的 `bionz.c` 代码）。

## ⚠️ 先看这里：本 PR 的最终状态


**这份 PR 报的是「查清了边界」，不是「跑通了」。** 请连带结论一起看：

| | 内容 |
|---|---|
| ✅ 已完成 | 装载器全链路、零崩溃、显示通路、boot_factor 补丁、`osal_uipc` 完整 cmd 0–16 语义表、`dmp_allocator` 绕过、探针 v2（TID/时间戳/qid） |
| ❌ 未达成 | **UI 从未开始绘制**。App 在 uipc 接收调用上进入 D 态阻塞；具体缺失环节尚未闭环（见顶部勘误） |
| 🛑 当前瓶颈 | `d3_diag17.txt` T+26.59s：`InfraOnDmdLdr` 与 `ACCESSLOG_MAIN` 均阻塞在 `osal_rcv_msg_tmo`，主线程 `sleep_on_page`。按需加载器卡住 ⇒ `appFw.so` 未加载 |
| ➡️ 下一步 | ① 继续 QEMU 侧间歇性推进（SKIPDMP 后追 appFw 加载链）② 把 IPC 依赖图与探针提供给有真机的团队 |

**★ 2026-11-04 更正**：原来此处的「晚间更新」引用了上游两条已撤回结论（渲染成功 / 输入队列），
整段删除并改见顶部勘误表。我们不再主张 BLE/uipc 阻塞是共性。

## TL;DR（成果，截至 2026-10-27 晚）

1. **装载器三道 magic 全部解码并放行** → `DMP_2D_Initialize : 0`，App 能走到 appFw/stella
2. **★ 零崩溃里程碑** — NULL 保护补丁后 App 稳定跑 150 秒，107 线程全就位
3. **显示通路打通** — VIP 渲染成功，但**画的是我们写进去的 320x240 测试彩条，不是 App 的 UI**（framebuffer -> VIP -> screendump 端到端验证）
4. ~~**门闩锁定 = boot_factor**~~ — **此结论已被 10-28 的 ioctl 探针否定**：反汇编 `appFw+0x17a1ae6` 显示 `get_boot_factor` 之后是 9 个无分支连续调用，**没有任何指令在读 key/type**。它只是被写进结构往后传，不是卡点。真正的卡点是 uipc 应答缺失（见下节）
5. **backup_db 基础设施**（生成器/校验器）—— 合成 DB 258 个 id 自检通过
6. **fontlist fallback 修法**（已实测 abort 消失）
7. **6 个二进制补丁**（libObj / backup.ko / stream.ko），均带文件字节序指纹校验

## 与已有工作的对照

| 已有 | 本 PR | 状态 |
|---|---|---|
| DMM 协议 RE | 未做 | 已有领先 |
| grm_ma 分派表 | 未做 | 已有领先 |
| 真机取证 | 无相机 | 已有领先 |
| 装载器三道 magic | 全过 + DMP_2D : 0 | **本 PR 领先** |
| **零崩溃 + 107 线程** | **150s 零崩溃，全服务就位** | **本 PR 领先** |
| **显示通路验证** | **VIP 测试图案渲染成功** | **本 PR 领先** |
| ~~boot_factor 门闩~~ | ~~因果链完整锁定~~ → **已被推翻**（不是卡点） | ❌ 我们自己的结论作废 |
| backup_db 基础设施 | 258 条自检通过 | **本 PR 领先** |
| fontlist fallback | abort 消失（实测） | **本 PR 领先** |
| VM_PFNMAP 根治 | Bad page map 60->0 | **本 PR 领先** |
| **新菜单 UI 是否真的出现** | **没有** —— 停在 uipc 等待 | ⚠️ **双方都未达成，需真机验证** |
| **能否在真机上复现/取证** | 无相机 | 已有领先 |

> 上表的「本 PR 领先」只覆盖装载器与观测手段这一层，**不代表新菜单移植成功**。

## 本次更新（2026-10-28）：uipc 卡点彻底定位，转入间歇性推进

> 完整交接：**[`contrib/osal-uipc-analysis/HANDOFF_20261028.md`](https://github.com/mizmo635/zve10-qemu/blob/zve10-contrib-loader-and-backup/contrib/osal-uipc-analysis/HANDOFF_20261028.md)**
> 工具集：**[`contrib/osal-uipc-analysis/`](https://github.com/mizmo635/zve10-qemu/tree/zve10-contrib-loader-and-backup/contrib/osal-uipc-analysis)**

### 一句话

App 不是"慢"、不是"饿死"、不是"缺模块"、不是"注册被拒"，而是
**在 uipc 接收调用（`cmd 7 = __k_4u_rcv_msg_tmo`，无限超时）上进入 D 态阻塞**：
`d3_diag17.txt` T+26.59s 显示 `InfraOnDmdLdr` 与 `ACCESSLOG_MAIN` 的 wchan 均为 `osal_rcv_msg_tmo`。
另注：T+18.65 → T+26.59 之间 `voluntary_ctxt_switches` 从 240 涨到 7380，
此前「T+6s CPU 归零」的描述偏早，需重测。

> ⚠️ 本段原先的「2425 次请求 / 72 次应答 / 3% 应答率」与「`13A` 请求 ↔ `139` 应答配对」
> 均已被 10-28 证据审计推翻（右删失统计错误；两队列都是日志服务监听队列）。详见顶部勘误表。

### 三条硬结论

1. **`osal_uipc.ko` 的 cmd 表全部解出**（0–16），ioctl 入口在 `__k_uipc_cdev_ioctl @ 0x5a8`，
   跳转表在 `.rodata+0x28`。
   **cmd 7 = `__k_4u_rcv_msg_tmo`**（阻塞收消息），我们卡的就是它。
2. ~~**请求/应答队列是配对的**~~ **已撤回（见顶部勘误）**：`13A` 与 `139` 都是日志服务的监听队列。
   cmd 7 = `__k_4u_rcv_msg_tmo` 的无限超时等待确实存在，但不能据此推断两队列配对。
3. **真实失败率几乎为 0**（11389 次调用里只有 7 次 `ret=-1`）。
   ⇒ 卡死不是"返回错误"，是"永不返回"；**"注册被拒"假设排除**。

### 附带的两个副产物

- **`Brought up 1 CPUs` 实证**：本机型内核虽是 SMP 版但只激活 1 核
  ⇒ 上游那条 `-smp 2` 经验在自研 `cxd90045` 机型上**不可能实现**（除非改内核平台代码）。
- **日志诊断的两个坑**：`loglevel=3` 会把 klog 清空；eMMC tuning 警告每 1–2 ms 一条，
  会把默认 1 MB 的 klog 冲满（App 启动那 6 秒必丢）。把 `klog.size` 改成 8 MB 才够。

### 当前瓶颈与下一步

要让 App 继续，需要让**请求处理器真正回复** —— 这需要再逆向一层 ulogio 请求协议
（消息格式、日志设备、可能的用户态配合）。而过了这关后面还有
显示初始化（DMP/GLES/GPE）→ uxengine → 资源加载 → 视图层。

**我们仍在间歇性推进**：`SKIPDMP=1` 已绕过 `dmp_allocator` 断言，App 进入库加载阶段（9 线程 / 14.4 MB RSS）。
（此处原有「上游首次渲染出新菜单」的呼应，2026-11-04 依上游撤回声明删除。）
QEMU 这条线的另一个价值是**安全沙盒**（能做真机不敢做的实验）。

---

## 本次更新（2026-10-28 下午）：SKIPDMP 绕过第一道门，App 进入库加载阶段

### 新增实验结果

**1. `SKIPDMP=1` 绕过 `dmp_allocator_lib_init` 断言**

`dmp_allocator`（在 `libObj.so` 里，管理 DMP 2D 加速器内存）的 `MA_ErrNone` 断言是第一道门。
用 `patch_libobj_skipdmp.py` 把 `grm_init` 里的 `bl DMP_2D_Initialize` NOP 掉后：
- `app.err` 从 116 B（断言崩溃）→ 54 B（正常输出）
- App 进入库加载：T+2s 1 线程/1.9MB → T+26s **9 线程/14.4MB RSS**，持续做 uipc 调用
- 但 `appFw.so` 仍未被 QMP 扫描器找到（400 秒/75 轮 MISS）

**2. 探针 v2（`core-sync-trace-v2.c`）**

- 加 TID / CLOCK_MONOTONIC 微秒时间戳 / run-id / 逐次 qid / cmd 7 timeout / TRUNC 标记
- **踩坑**：`SYS_clock_gettime` / `SYS_gettid` 宏在交叉工具链里展开为空 ⇒ 必须硬编码 ARM EABI 号
- 实测 **1447 行 trace / 49 KB**，App 在跟 `0x8F013A`（日志服务）和 `0x3E014D`（backup）活跃通信

**3. `best3` 模式对照（推翻"objset 补丁致崩"假设）**

| 模式 | libObj 补丁 | backup.ko 补丁 | input.ko | App 表现 |
|---|---|---|---|---|
| `objset` | ✅ 3 个 | ❌ | 保留 | 加载 appFw → 崩在 dmp_allocator |
| `best3` | ❌ | ✅ 2 个 | 删除 | **appFw 根本没加载** |
| `objset+SKIPDMP` | ✅ | ❌ | 保留 | 过 dmp_allocator → 库加载中（9 线程） |

⇒ **`objset` 的 libObj 补丁是必要的**，没有它 App 更早就死。

**4. 观察者存活问题**：`( ... ) &` 子 shell 在 `exec` 后被杀。`trap HUP` / `setsid` / `disown` 均不完全可靠。
最终方案：`dump_all` 合并进 `dm.sh`（与 maps 序列共用），`&` + `disown` 启动。实测抓到 2 次心跳后仍被杀（FAT 写入慢）。

### 新增文件

- `core-sync-trace-v2.c` — 探针 v2（TID/时间戳/qid）
- `diag-app.sh` — 诊断启动器（替换 `im.elf`，合并 dm.sh 观察者）
- `patch-zv1f-mon.sh` — 补丁集（objset/best3/SKIPDMP 模式）
- `patch-bootfactor.py` — boot_factor 补丁（接受树路径参数）

### 与上游发现的关系（2026-11-04 更正）

本节原引用的两条结论均已被上游撤回，整节不再成立。详见顶部勘误表。
我们当前与上游可交叉验证的部分是：`Brought up 1 CPUs`（对方确认一致）、
以及 `grm_gles.ko` 中断面（见 `contrib/gpe-ioctl-package/`）。

---

## 本次更新（2026-10-27 晚）

### 零崩溃里程碑

给 `libInfraBluetoothLowEnergy.so` 析构函数打 NULL 保护补丁（@0x5102a `cbz->b.n`），App 从「13 秒崩」一路到「稳定 150 秒」：

```
150 秒运行：零崩溃，register dump = 0
107 个线程：AppFw / uxengine_res1-2 / IWrapper* / SeqQueue* / grm_* / 全部就位
151 个组件库：gui / 蓝牙 / 网络 / 更新 全部加载
```

### 显示通路验证

| 组件 | 地址 | 状态 |
|---|---|---|
| GPE/Sugilite (GPU) | 0xF1000000 | 活跃（测试命令） |
| cpyfb (拷贝引擎) | 0x79700000 | 实现完整，0 次调用（App 未触发） |
| VIP (扫描输出) | 0x79800000 | 320x240 QemuConsole，RGBA4444/YCbCr422 |
| vsync | CXD90045 漏连 | 已加 60Hz 定时器修复 |

**framebuffer 覆盖实验**：写 RGBA4444 彩色测试条到 0x3F000000，VIP 渲染到 screendump 可见。**显示通路完全可用。**

坑：`vip_reset()` 在 `vip_realize()` 后清零通道配置；VIP channel enable 是 self-clearing（每帧重配）。

### boot_factor 门闩（当前卡点）

从 `appFw.so` 字符串提取的因果链：

```
QEMU 无输入设备 (/proc/bus/input/devices = 空)
  -> UPM (118KB, 已加载) 拿不到 GPIO/ADC 按键
    -> get_boot_factor 失败
      -> Application_Boot_Select 不知道进什么模式
        -> 状态机停在 boot 阶段，不实例化屏幕
          -> 黑屏（但 107 线程全活着）
```

关键字符串：`[Boot_Factor] bis:%08x attr:%08x` / `Not support boot key` / `UPM_CMD_*`。

### 对上游进展的看法

注意到今天有 8 个提交（`dmm.ko` VA 越界 / initrd 无 shell / 内存 reservation）。我们这边的 `boot_factor` 发现可能与 `dmm.ko` 加载问题同族——都是电源/显示管理模块初始化序列的问题。**双方互补空间大。**

## 关键发现（原始）

**装载器有三道 magic（不是两道）**：

```
1) BIN! @buf[0]  0x206d54  失败码 r4=-1  -> DISCRIM 放行
2) CNF! @buf[16] 0x206d82  失败码 r4=-8  -> BYPASSCNF 放行
3) PTN! @buf[20] 0x206e9c  失败码 r4=-9  -> BYPASSCNF 放行（在循环之后，容易漏）
```

**DISCRIM 的 NOP bug**：`bf 00 bf 00` 是两条 `lsls r7,r7,#2` 而不是 NOP，装载器帧指针会左移 4 位。正确写法是 `00 bf 00 bf`（逐半字反转）。已加文件字节序指纹校验防止再犯。

**fontlist fallback 是字面量 fopen("TEXTRM_FONT_LIST")**，不是 getenv —— export 环境变量是死代码。修法：在候选 cwd 创建该名字的文件指向真实 fontlist.dat，实测 TextRM abort 消失。

## 文件清单

- `contrib/PR_DESCRIPTION.md` — 本 PR 描述（含 2026-11-04 勘误）
- `contrib/gpe-ioctl-package/` — **GPE 设备模型补丁 + `grm_gles.ko` 全 17 条 ioctl 注解表**（纯文本与我们自己的代码，不含 Sony 二进制）
- `contrib/osal-uipc-analysis/` — **uipc 观测工具集**（见下）
  - `HANDOFF_20261028.md` — 完整交接文档（含 cmd 0–16 语义表、13A→139 配对、vblock 高水位语义）
  - `core-sync-trace.c` / `core-sync-trace-v2.c` — LD_PRELOAD 探针（v1 基础版 / v2 加 TID/时间戳/qid）
  - `diag-app.sh` — 诊断启动器（替换 `im.elf`，含 dm.sh 合并观察者）
  - `patch-zv1f-mon.sh` — 补丁集（objset / best3 / SKIPDMP 模式）
  - `patch-bootfactor.py` — boot_factor 补丁（伪造 `key=0/type=2`）
  - `rebuild-with-trace.sh` — 重建 → 装探针 → 打补丁 → 跑 → 抽日志
  - `run-klog.sh` / `run-long-window.sh` / `run-plain-control.sh` / `run-breakpoint-dump.sh`
  - `bf-dump.gdb` / `scan-dump.gdb`
- `contrib/patches/` — 6 个补丁（字节序指纹校验）
- `contrib/tools/` — backup_db 生成器/校验器、fontlist 验证脚本

感谢你们在 DMM 协议、grm_ma 分派表和真机取证上的工作——那几块是我们没有的，
也感谢 11-04 那条逐条列出证据的更正。

本 PR 现在的可用产出是：`osal_uipc.ko` cmd 0–16 语义表、v2 探针（TID/时间戳/qid）、
以及 `contrib/gpe-ioctl-package/` 里的 `grm_gles.ko` 全 17 条 ioctl 注解与 GPE 设备模型补丁。
后者针对你们「中断送达但等完成位的 ioctl 永不返回」的卡点。

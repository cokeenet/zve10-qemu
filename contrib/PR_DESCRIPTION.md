来自 ZV-1F 新菜单移植项目的独立验证团队。我们独立走通了装载器全链路，App 稳定运行 150 秒零崩溃 —— 但**这条线最终停在了 uipc 应答缺失上，我们已经停手**。

## ⚠️ 先看这里：本 PR 的最终状态

**这份 PR 报的是「查清到哪里走不通」，不是「跑通了」。** 请连带结论一起看：

| | 内容 |
|---|---|
| ✅ 已完成 | 装载器全链路、零崩溃、显示通路、boot_factor 因果链、`osal_uipc` 完整 cmd 表 |
| ❌ 未达成 | **UI 从未开始绘制**。App 发了 2425 次请求只等到 72 次应答（3%），然后无限超时永久等待 |
| 🛑 我们的结论 | **建议在 QEMU 线停手**。再往下要重造 ulogio 请求协议，属新项目级投入，且后面还有显示初始化 → uxengine → 资源加载 → 视图层若干关 |
| ➡️ 建议的下一步 | 把这份 IPC 依赖图交给**有真机**的团队，用同一个探针抓一次 `0x8F0139` 的应答长什么样 |

**新菜单在 QEMU 里从未被任何人跑通过。** 我们做到的是把卡点从"黑屏"压到"一个队列无人应答"，不是把它跑起来。

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

## 本次更新（2026-10-28）：uipc 卡点彻底定位，并建议在 QEMU 线停手

> 完整交接：**[`contrib/osal-uipc-analysis/HANDOFF_20261028.md`](https://github.com/mizmo635/zve10-qemu/blob/zve10-contrib-loader-and-backup/contrib/osal-uipc-analysis/HANDOFF_20261028.md)**
> 工具集：**[`contrib/osal-uipc-analysis/`](https://github.com/mizmo635/zve10-qemu/tree/zve10-contrib-loader-and-backup/contrib/osal-uipc-analysis)**

### 一句话

App 不是"慢"、不是"饿死"、不是"缺模块"、不是"注册被拒"，而是
**向 uipc 队列 `0x8F013A` 发了 2425 次请求，只在应答队列 `0x8F0139` 上等到 72 次回复（3%），
然后用无限超时永久等待**。

### 三条硬结论

1. **`osal_uipc.ko` 的 cmd 表全部解出**（0–16），ioctl 入口在 `__k_uipc_cdev_ioctl @ 0x5a8`，
   跳转表在 `.rodata+0x28`。
   **cmd 7 = `__k_4u_rcv_msg_tmo`**（阻塞收消息），我们卡的就是它。
2. **请求/应答队列是配对的**：`13A`（大块、2 个监听者）= 请求，`139`（小块、1 个监听者）= 应答。
   App 在 `139` 上收，`timeout = 0xffffffff`（无限）。
3. **真实失败率几乎为 0**（11389 次调用里只有 7 次 `ret=-1`）。
   ⇒ 卡死不是"返回错误"，是"永不返回"；**"注册被拒"假设排除**。

### 附带的两个副产物

- **`Brought up 1 CPUs` 实证**：本机型内核虽是 SMP 版但只激活 1 核
  ⇒ 上游那条 `-smp 2` 经验在自研 `cxd90045` 机型上**不可能实现**（除非改内核平台代码）。
- **日志诊断的两个坑**：`loglevel=3` 会把 klog 清空；eMMC tuning 警告每 1–2 ms 一条，
  会把默认 1 MB 的 klog 冲满（App 启动那 6 秒必丢）。把 `klog.size` 改成 8 MB 才够。

### 为什么建议停手

要让 App 继续，必须让**请求处理器真正回复** —— 这需要再逆向一层 ulogio 请求协议
（消息格式、日志设备、可能的用户态配合），属新项目级投入。而过了这关后面还有
显示初始化（DMP/GLES/GPE）→ uxengine → 资源加载 → 视图层；**新菜单在 QEMU 里从未有人跑通过**。

**真正的价值在于把这份依赖图交给有真机的团队**：用同一个探针在真机上抓一次
`0x8F0139` 的应答长什么样，一次采样就能省掉 QEMU 侧几天的猜测。
QEMU 这条线的定位应该是**安全沙盒**（能做真机不敢做的实验），而不是必须画出画面。

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

- `contrib/PR_DESCRIPTION.md` — 完整发现文档（含 Backup_read 全链路、Data_SearchId 算法、三道 magic 控制流）
- `contrib/patches/` — 6 个补丁（字节序指纹校验）
- `contrib/tools/` — backup_db 生成器/校验器、fontlist 验证脚本

感谢你们在 DMM 协议、grm_ma 分派表和真机取证上的工作——那几块是我们没有的。今天看到内核+initrd 在我们的机器模型上跑起来了 UI init，这是很好的合流信号。

来自 ZV-1F 新菜单移植项目的独立验证团队。我们独立走通了装载器全链路，**现在 App 已稳定运行 150 秒零崩溃**，成果与本项目互补。

## TL;DR

1. **装载器三道 magic 全部解码并放行** → `DMP_2D_Initialize : 0`，App 能走到 appFw/stella
2. **★ 零崩溃里程碑** — NULL 保护补丁后 App 稳定跑 150 秒，107 线程全就位
3. **★ 显示通路打通** — VIP 320x240 测试图案渲染成功（framebuffer -> VIP -> screendump 端到端）
4. **★ 门闩锁定 = boot_factor** — App 启动时读启动按键（`Application_Boot_Select`）决定进什么模式，QEMU 无输入设备 -> 停在 boot 阶段
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
| **boot_factor 门闩** | **因果链完整锁定** | **本 PR 领先** |
| backup_db 基础设施 | 258 条自检通过 | **本 PR 领先** |
| fontlist fallback | abort 消失（实测） | **本 PR 领先** |
| VM_PFNMAP 根治 | Bad page map 60->0 | **本 PR 领先** |

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

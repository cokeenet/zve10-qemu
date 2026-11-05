# ZV-1M2 新菜单 UI → ZV-E10 一代 · 移植清单

**日期**：2026-11-05 · **数据来源**：`/mnt/build/fw/COMPARE/extracted/{ZV1M2,ZVE10,ZVE10M2}`

---

## 一、结论先行

### ★ 两代的 UI 架构是**同一套框架的拆分方式不同**

| | ZV-E10 一代 | ZV-1M2 | ZV-E10 II |
|---|---|---|---|
| 图形后端 | **ASTRA** (CXD90045) | **ASTRA** (CXD90045_k8) | IPSX |
| UI 主体 | `libObj.so` 20.3 MB + **`viewUnified2..8.so` 21 MB** | `appFw.so` 64 MB + `appFwBoot.so` 8 KB | `appFw.so` 94 MB |
| 界面代码位置 | **分散在 viewUnified*.so** | **集中到 appFw.so** | 集中到 appFw.so |
| `.uxc` 数量 | 299 | 220 | 220 |
| `master_camera.uxc` | 101804 B | **101868 B**（335 项中 330 项相同） | 101868 B（与 ZV1M2 逐字节相同） |
| glibc / 内核 | 2.11 / 3.0.27 | 2.11 / 3.0.27 | 2.25 / 4.9 |

**⇒ ZV-1M2 与 ZV-E10 一代的菜单是「同一代框架（ASTRA）的不同打包方式」。
   ZV-E10 II 才是异类（IPSX + glibc 2.25 + aarch64）。**

---

## 二、移植清单（按必要性排序）

### A. 必须替换（ZV-1M2 独有，ZVE10 没有）— 30 个库

```
核心 UI：
  + usr/lib/appFw.so            64 MB   ← 新菜单 UI 主体
  + usr/lib/appFwBoot.so         8 KB   ← 启动器（dlopen appFw.so 并建线程）
  + usr/lib/AdjChkConfig.so    82 KB   ← 调整检查 UI（appFwBoot 的另一个分支）

基础设施（新菜单依赖）：
  + usr/lib/libuiobserve.so
  + usr/lib/libInfraBluetoothClassic.so
  + usr/lib/libInfraCrypt.so / libInfraCryptCore.so
  + usr/lib/libInfraNetworkCommon.so
  + usr/lib/libNetworkMsgHandler.so
  + usr/lib/libInfraAppRemoteBoard.so
  + usr/lib/libCertificateManager.so / libInfraMFi.so
  + usr/lib/libUniqIDGen.so
  + usr/lib/fp7.so / samp.so / samp2.so
  + usr/lib/libInfraCCPF.so / libInfraCCPFStatic.so
  + usr/lib/libInfraNetRfTest.so

云端/网络（ZV-1M2 新增功能，可按需）：
  + usr/lib/libawsiotsdk.so
  + usr/lib/libkvsWebrtcClient.so / libkvsWebrtcSignalingClient.so
  + usr/lib/libwebsockets.so.18
  + usr/lib/netconf/dhcpd_no_dns.conf

程序：
  + usr/bin/app_monio.elf / mpr_monio2.elf / checkServerCert
```

### B. 需要保留或适配（ZVE10 独有，ZV-1M2 没有）— 18 个

```
★ 关键：这些是 ZV-E10 一代的**原生菜单界面**
  - usr/lib/viewUnified2.so   14.3 MB   ← ★ 一代 UI 主体
  - usr/lib/viewUnified3.so    0.94 MB
  - usr/lib/viewUnified4.so    3.3 MB
  - usr/lib/viewUnified5.so    0.75 MB
  - usr/lib/viewUnified6.so    0.59 MB
  - usr/lib/viewUnified7.so    0.58 MB
  - usr/lib/viewUnified8.so    0.28 MB
  ⇒ 合计约 21 MB，导出的是 CmnView*/LG_master_*/ACT_Icon* 等菜单类

  其它一代原生：
  - usr/lib/libBizFw.so / libBizFw2.so      ← 业务固件
  - usr/lib/libmprctrl.so / libmprdummy.so  ← 影像处理
  - usr/lib/libInfraBGMMeta.so / libInfraCacheMgr.so
  - usr/lib/libInfraHighlight.so / libInfraMakeScenario.so
  - usr/lib/CautionConfig.so / CautionRsrcConfig.so / ModelCautionConfig.so
```

### C. 可以直接复用（两边相同）

```
✅ usr/kmod 中 78 个 .ko 字节数完全相同（102/103 同名，vermagic 100% 一致）
✅ master_camera.uxc 335 项中 330 项相同
✅ LayoutMaster_Menu.uxc：ZV1M2/ZVE10M2 完全相同（3636 B），ZVE10 略小（3512 B）
✅ ZV1M2 的 image 包就是 *_ASTRA.uxc（与一代图形后端同名）
```

---

## 三、移植的核心矛盾（必须解决）

```
ZV-1M2 的 appFw.so（64 MB）**包含了完整的 UI，不再依赖 viewUnified*.so**。
ZV-E10 一代的 libObj.so 期望 UI 在 viewUnified*.so 里。

⇒ 两条可能路线：
  路线 1（推荐）：**整套替换 UI 层**
     appFw.so + appFwBoot.so 替换掉 viewUnified2..8.so 的角色
     即：让 im.elf 用 appFwBoot.so 而不是加载 viewUnified*
     
  路线 2：**混合**
     保留 viewUnified*（一代原生功能），另加 appFw.so 提供新菜单
     ⇒ 两者会争夺同一个 WidgetSystem，风险极高
```

---

## 四、`.uxc` 数据层对比（已解析格式）

```
格式：
  0    4 B  magic 'uxc\x07'
  8    2 B  版本 = 8
  10   2 B  字段A
  12   2 B  条目数 N
  16  N×2 B 偏移表（u16 LE，0xFFFF=空槽）
  16+N*2 起 条目结构体

master_camera.uxc 差异（ZV1M2 vs ZVE10）：
  335 项中 330 项完全相同
  仅 5 项不同：idx 52(-27B) / 92(+17B) / 254(+18B) / 280(+8B) / 334(+48B)

string_english.uxc 差异：
  ZV1M2   852 条可读文案（46552 B）
  ZVE10  3499 条可读文案（174104 B）
  ZV1M2 独有 37 条（镜头补偿/连拍新档/CFexpress/网络FTP/对焦保持按钮/Clip Flag）
```

---

## 五、下一步

```
1. 确认 im.elf 是否硬编码依赖 viewUnified*.so
   （若是，路线 1 需要同时替换 im.elf）
2. 确认 appFwBoot.so 的入口条件（它由谁 dlopen）
3. 对比 ZVE10 一代的 im.elf 与 ZV-1M2 的 im.elf
4. 决定路线 1 还是 2
```

---

## 六、★ 四项确认结果（2026-11-05 第 67 轮）

### 1) `im.elf` **不依赖** `viewUnified*.so`，也不依赖 `appFw.so`
```
ZVE10  im.elf (23240 B) md5=f1c746e3…
ZV1M2  im.elf (23240 B) md5=d2f47a82…
⇒ 大小相同、内容不同；NEEDED 完全一致（libIMDB/libbackup/libosal_* /libdl/...）
⇒ 字符串里只提到 **/usr/lib/libObj.so**，完全没提 viewUnified 或 appFw
⇒ **im.elf 是通用加载器，加载什么由 libIMDB 的清单决定**
```

### 2) ★★★★ `libIMDB.so` 才是真正的"加载清单"，两代入口名不同
```
ZVE10  libIMDB.so  37204 B  md5=7581e5cd…   清单里有: **appFw.so**
ZV1M2  libIMDB.so  40596 B  md5=91eca62f…   清单里有: **appFwBoot.so**
⇒ **两代都通过 appFw 加载 UI，只是入口名不同。**
```

### 3) ★★★★ 但 **ZVE10 一代的 `appFw.so` 文件在所有已知固件里都不存在**
```
全盘搜索 appFw.so 结果：
  ZV1M2      64273528 B   ✅ 存在
  ZVE10M2    93620512 B   ✅ 存在
  ZV1F       53189440 B   ✅ 存在
  ZVE10      —            ❌ **不存在**
  
  （另有 port_m2/out、emu/root、port_v2/out 等我们自己的实验产物）
  
原始固件 ZVE10_V203_unpacked 全盘搜索：也**没有** appFw.so。
⇒ **这与 ZV1M2 的情况完全一致**（第 135 层已证：IMDB 清单是模板，
   191 项里有 118 项在固件中不存在）。
```

### 4) ZVE10 一代的 UI **确实在 viewUnified*.so 里**
```
libObj.so NEEDED（20 项）：
  libBizFw.so（业务固件，ZVE10 独有）
  libmprctrl.so / libmpr.so（影像处理）
  libMWF.so / libsencore.so / libBackupManager.so …
  ⇒ **不依赖 viewUnified**（说明 viewUnified 是被 IMDB 按需加载的，不是硬依赖）

viewUnified2.so NEEDED（仅 3 项）：
  CautionConfig.so      ← ★ ZVE10 独有（ZV-1M2 没有）
  libgcc_s.so.1
  libc.so.6
  ⇒ viewUnified 依赖 CautionConfig.so

ZV-1M2 的 libIMDB 清单里**没有** viewUnified*、CautionConfig*
ZVE10 的 libIMDB 清单里**有** appFw.so、gui.so、libgpe.so、libgrm.so（但文件都不存在）
```

---

## 七、结论：移植路线明确

### 路线 1（推荐）：把 ZV-1M2 的整个 UI 栈搬过去

```
必须替换的入口：
  ZVE10 的 libIMDB.so  → ZV1M2 的 libIMDB.so
    （因为清单里要 appFwBoot.so 而不是 appFw.so）

必须新增的库（ZV-1M2 独有的 30 个）：
  appFw.so / appFwBoot.so / AdjChkConfig.so / libuiobserve.so / …

可以删除/保留的（ZVE10 独有 18 个）：
  viewUnified2..8.so + CautionConfig*.so + libBizFw* + libmprctrl …
  ⇒ **建议保留**（一代原生功能仍需要 libBizFw/mprctrl），
     只让新 UI 接管 WidgetSystem 层
```

### 关键风险点

```
⚠️ 1. `libIMDB.so` 换掉后，ZVE10 原生的 168 个库里有 18 个不在 ZV1M2 清单里
      ⇒ 那些原生功能（闪光灯/AF/PAL 等）会失去加载入口

⚠️ 2. ZV-1M2 的 `appFw.so` 是否依赖 `viewUnified*` 里才有的类？
      已验证：appFw.so 的 15 个 NEEDED 里没有 viewUnified ✅

⚠️ 3. `CautionConfig.so` 只有 ZVE10 有，而 viewUnified2 依赖它
      ⇒ 若保留 viewUnified，必须保留 CautionConfig
```

### 推荐的移植顺序

```
① 备份 ZVE10 的 libIMDB.so
② 放入 ZV-1M2 的 30 个独有库
③ 换 libIMDB.so 为 ZV-1M2 版
④ 保留 ZVE10 的 viewUnified*/CautionConfig*/libBizFw*（避免丢原生功能）
⑤ 保留 kmod（已证 102/103 同名，78 个字节完全相同）
⑥ 换 /setting/*（ZV-1M2 同代）
⑦ 上机测试
```

---

## 八、可执行脚本（2026-11-05 第 68 轮）

### 脚本：`scripts/port_m2menu.sh`

```bash
./port_m2menu.sh <输出目录>
```

**流程（7 步）：**
```
① 铺 ZVE10 一代完整基准树（保留原生功能）
② 覆盖 usr/kmod 为 ZV-1M2 版（vermagic 100% 一致）
③ 放入 ZV-1M2 的 UI 栈：appFw.so / appFwBoot.so / AdjChkConfig.so / libuiobserve.so
④ 补齐 ZV-1M2 独有的依赖库
⑤ 换 libIMDB.so（清单入口 appFw.so → appFwBoot.so）
⑥ 替换 usr/share/app 菜单数据（含 6 个 *_ASTRA.uxc）
⑦ 校验：关键文件存在 + appFw.so 的 NEEDED 全部齐备
```

### 实测结果（`/mnt/build/m2qemu/port/PORTED`）

```
✅ 移植树组装完成
   文件总数: 864
   总大小:   319 MB

   usr/lib       188 个（ZVE10=168 + ZV1M2 新增 20）
   usr/kmod      103 个（ZV-1M2 版）
   usr/share/app 232 个（ZV-1M2 版，含 6 个 *ASTRA.uxc）

关键校验：
   ✅ libIMDB 清单入口 = **appFwBoot.so**（已从 appFw.so 切换）
   ✅ appFw.so 64,273,528 B，**NEEDED 15 项全部齐备**（含 AdjChkConfig.so）
   ✅ appFwBoot.so 8,312 B
   ✅ 6 个 image_*_ASTRA.uxc
   ✅ 保留 ZVE10 原生：viewUnified ×7、CautionConfig ×2
   ⚠️ im.elf 仍是 ZVE10 版（f1c746e3）—— 因为 im.elf 只依赖 libObj.so，
      加载清单在 libIMDB 里，所以**不需要换 im.elf**
```

### 与三方的对比

| 目录 | 移植树 | ZVE10 | ZV1M2 |
|---|---|---|---|
| `usr/lib` | **188** | 168 | 149 |
| `usr/kmod` | 103 | 103 | 103 |
| `usr/share/app` | 232 | 308 | 232 |

**移植树 = ZVE10 的全集 + ZV-1M2 独有的 20 个库，符合设计意图。**

### 下一步

```
这个移植树是**可上机的成品**，但 QEMU 里验证它需要解决第 181 层的同一个阻塞
（Application_Start 在 site2 之后不继续）。

⇒ 两条并行：
   A. 把这个树做成可在真机 /usr 上部署的镜像（用户真正的目标）
   B. 继续在 QEMU 里排查 site2 的阻塞（若解决，就能在 QEMU 里看到新菜单）
```

---

## 九、★ 两代启动架构的根本差异（2026-11-05 第 69 轮）

### 这是本次移植最关键的结构性发现

```
ZVE10 一代：
  im.elf → libIMDB 清单 → **libObj.so** 导出 Application_System_Init(144B)
                            libObj.so = 20.3 MB（含全部 UI）
  ⇒ 应用入口在 libObj 里

ZV-1M2：
  im.elf → libIMDB 清单 → **appFwBoot.so** 导出 Application_System_Init(284B)
                            appFwBoot.so = 8,312 B
                            libObj.so = 3.9 MB（**纯引擎，无应用入口**）
  ⇒ 应用入口在 appFwBoot 里，它再 dlopen("appFw.so") + dlsym("Application_Start")
```

### 两代 libObj.so **不是同一个东西**

| | ZVE10 | ZV1M2 |
|---|---|---|
| 大小 | **21,305,528 B (20.3 MB)** | **3,931,624 B (3.9 MB)** |
| md5 | `12cf982d…` | `738a2331…` |
| `Application_System_Init` | ✅ 导出（144 B） | ❌ **没有** |
| `uxengine_init` | ✅ 导出（6 B） | ✅ 导出（6 B） |
| `uxengine_init2` | ❌ **没有** | ✅ 导出（216 B） |
| 定位 | 引擎 + 全部 UI | **纯引擎** |

### `appFwBoot.so` 的完整导出（8,312 B）
```
Application_System_Init   284 B   ← 应用入口
Main                      120 B   ← 线程体
initializeArena           332 B
+ stella::BitArray<u32/u16> 的各种弱符号（内联展开）

导入的 osal 符号：
  osal_valloc_msg_wait / osal_snd_sync_msg / osal_free_msg
  osal_create_waitable_thread / osal_set_cpu
字符串：
  "appFwBoot.so" / "appFw.so" / "Application_Start"
```

### ★ 这意味着移植**必须成组替换**
```
❌ 不能只换 appFw.so：
   ZVE10 的 libObj.so 里没有 uxengine_init2，
   而 appFwBoot.so 的 Main 会 blx uxengine_init2 ⇒ 找不到符号 ⇒ 加载失败

✅ 必须同时换：
   libObj.so      (ZVE10 20.3MB → ZV1M2 3.9MB)
   appFwBoot.so   (新增)
   appFw.so       (新增 64MB)
   libIMDB.so     (清单入口改 appFwBoot.so)
   + appFw.so 的 15 个 NEEDED（14 个 ZVE10 已有，1 个 AdjChkConfig.so 需带）
```

### 对移植脚本的修正

**当前脚本（`scripts/port_m2menu.sh`）漏了 `libObj.so`** —— 这是致命遗漏。
必须加入第 3 步：用 ZV-1M2 的 `libObj.so` 覆盖 ZVE10 的。

但注意：`libObj.so` 从 20.3 MB 缩到 3.9 MB，**意味着 ZVE10 一代的原生 UI 类
（CmnView*/LG_master_*/ACT_Icon* 等，共 21 MB）会全部消失**。
那些类在 `viewUnified*.so` 里也有一部分，但 `libObj.so` 自己那 20 MB 里是什么需要确认。

⇒ **下一步必须先确认：ZVE10 的 libObj.so 那 20.3 MB 里到底装了什么。**
   如果它也含 UI 类，那换掉会丢掉大量原生功能。

---

## 十、★★★ 换 libObj 的代价评估（2026-11-05 第 70 轮）

### 1) 20.3 MB 与 3.9 MB 的差距在哪
```
ZVE10 libObj.so (21,305,528 B)      ZV1M2 libObj.so (3,931,624 B)
  .text      13.23 MB        vs        2.16 MB     ← 差 11 MB 代码
  .rodata     3.59 MB        vs        0.55 MB
  .ARM.extab  1.01 MB        vs        0.14 MB
  .ARM.exidx  0.47 MB        vs        0.13 MB
  .data.rel.ro 0.44 MB       vs        0.07 MB
  .data       0.47 MB        vs        0.44 MB（几乎一样）
```

### 2) ★ ZVE10 libObj 独有的 UI 类（ZV1M2 完全没有）
```
类名前缀                 ZVE10 libObj 导出数
  ViewBase                    69
  LayoutableWidgetBase        49
  AppWidgetBase               27
  DefRootView                 22
  CmnViewMenuData             21
  HighlightManager            18
  View                        13
  LkmIconguide                 8

⇒ 这些类在 **ZV1M2 的 libObj.so 和 appFw.so 里都不存在**
  （appFw.so 只在字符串里出现 0 次，且它只导出 1 个符号：Application 系列）
⇒ appFw.so 的 UI 类全是**内部符号，已 strip**
```

### 3) ★★★ wgtsys 引擎接口高度兼容（这是好消息）
```
ZVE10 libObj  wgtsys 导出 99 个
ZV1M2 libObj  wgtsys 导出 104 个
  完全相同 92 个（93%）
  只 ZVE10 有  7 个
  只 ZV1M2 有 12 个

只 ZVE10 有的 7 个（换掉会丢）：
  LayoutableWidgetBase::setDefWidgetMode
  LayerBase::moveLast
  WidgetBase::getID
  Widget::getTopHitMargin / getLeftHitMargin / getRightHitMargin / getBottomHitMargin

只 ZV1M2 有的 12 个（新接口，说明引擎确实升级了）：
  WidgetSystem::notifyDraw / updateFrame / processEvent / updateCurrentTime
  WidgetSystem::isFrameDelay
  WidgetBase::acceptMultiTouch / getTimerInfo / isMouseCapture
  Widget::Widget()  ← ★ **构造函数**（ZVE10 的 libObj 没有导出！）
  GestureAnalyzer::getPinchOriginPos / getDevicePos / isMultiTouch
```
**⇒ 第 164 层我推断"libObj 不导出 Widget 构造函数"是对的 —— 但那只对 ZVE10 成立；
   ZV1M2 的 libObj **确实导出** `Widget::Widget()`。**

### 4) 结论：移植必须整体替换，且会失去一代原生 UI
```
✅ 可行部分：
   · 引擎接口 93% 兼容（92/99 相同）
   · kmod 102/103 同名、78 个字节完全相同、vermagic 100% 一致
   · 菜单数据框架同构（master_camera 335 项中 330 项相同）
   · 图形后端同为 ASTRA

❌ 不可回避的代价：
   · 必须换 libObj.so（否则 appFwBoot 找不到 uxengine_init2）⇒ 丢 20.3 MB 的一代 UI 类
   · 必须换 libIMDB.so（清单入口不同）
   · 必须新增 appFwBoot.so + appFw.so（64 MB）
   · 一代原生功能（闪光灯/AF 模式/PAL/Caution 等）的 UI 入口全部消失

⇒ **这不是"给 ZVE10 加一个新菜单"，而是"把 ZVE10 的 UI 层整体换成 ZV-1M2 的"。**
```

### 5) 修正后的移植清单（必须项）
```
必须替换（缺一不可）：
  ① libObj.so        20.3 MB → 3.9 MB
  ② libIMDB.so       37204 B → 40596 B（清单入口 appFw.so → appFwBoot.so）
  ③ + appFwBoot.so   8,312 B（新增，导出 Application_System_Init）
  ④ + appFw.so       64,273,528 B（新增，导出 Application_Start）
  ⑤ + AdjChkConfig.so 82,440 B（appFw.so 的唯一缺失依赖）
  ⑥ usr/kmod/*.ko    全部换成 ZV-1M2 版（已证 verifiable）
  ⑦ usr/share/app/   全部换成 ZV-1M2 版（含 6 个 *_ASTRA.uxc）

可选保留：
  · viewUnified2..8.so（一代原生 UI，换 libObj 后可能无引用者）
  · CautionConfig*.so / libBizFw*.so（原生业务，视需求）
```

### 6) 对脚本的修正
```
scripts/port_m2menu.sh 需要加入：
  - 用 ZV-1M2 的 libObj.so 覆盖 ZVE10 的  ← **之前漏了，致命**
  - 校验 appFwBoot.so 的 uxengine_init2 能在新 libObj 里找到
```

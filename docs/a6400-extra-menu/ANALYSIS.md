# A6400-Extra-Menu 全仓库技术地图（严格研究版）

> 研究对象：https://github.com/PCIEARIA/A6400-Extra-Menu @ `d3b0b12`（首次公开，单一 commit）
> 77 个文件 / 552 KB。授权 PolyForm Noncommercial 1.0.0。
> 本文只记录**仓库里实际写着的**内容，不推测。

---

## 一、它到底做了什么（一句话）

**用 MENU＋放大鏡 组合键召唤一个"自制的原生图层菜单"，菜单本身是索尼自家 Widget 系统渲染的，
不是往 framebuffer 里画像素。** 菜单里管 LUT 开关和 5 个 Film 预设；关闭后原生按键/触控恢复正常。

**关键性质：**
```text
· 不改固件、不改分区、不改只读资源
· 不写 framebuffer（明确写明 "No framebuffer writes"）
· 不吞原生输入（"No input consumption"）
· 全部在 RAM 里：一次性 loader 把 payload 注入 im.elf 的 RAM，trial 时限到期自动撤
```

---

## 二、仓库文件逐个说明

### camera/（相机端源码，共 35 个文件）

| 文件 | 作用 |
|---|---|
| `film_menu.h/.c` | **纯逻辑菜单状态机**（无任何相机依赖，可在 host 测试）。负责打开条件、触控命中、页面、Close |
| `film_native_view.h/.c` | 菜单与原生绘制之间的桥：`step()` 决定何时 draw/hide/settings |
| `native_film_ui_trial3.c` | **核心：原生 UI 接驳**。用索尼自己的 Widget/Panel/Label/Layer 建菜单 |
| `native_ui_probe.c` | 版本校验探针（先跑这个确认 API 地址与单例） |
| `native_ui_load_payload.c` | **一次性 loader**：在原生 release 回调里注入并 dlopen 菜单 so |
| `native_ui_load_guard.c` | **冻结+ptrace 写入器**：把 payload 写进 im.elf 的 RAM |
| `native_freeze.h` | 有界 ptrace 线程冻结（PID 149 的 CPU 线程） |
| `native_start.S` / `native_ui_trampoline.S` / `native_ui_payload.ld` | 裸机入口、跳板、链接脚本（固定到 0x0700d708） |
| `film_settings_*` | 设置校验、快照、读写、发布（RAM 内） |
| `film_capture_daemon.c` | 新照片扫描 + 设置 gate + 串行派工 |
| `film_native_command.c` | 原生文件命令转接（读原片、另存） |
| `film_ram_job.sh` | 工作模板：原片与输出分离、哈希校验、失败保留现场 |
| `ui_trial_trace.h` / `ui_input_journal.h` | 诊断记录（固定地址的 trace 结构） |
| `test_film_menu.c` / `test_film_native_view.c` | 相机端单元测试（可在 host 编译） |

### host-tests/（编译与打包，共 12 个）

| 文件 | 作用 |
|---|---|
| `build_ui_trial3.sh` | 编译原生 UI 菜单 so + payload.bin；**带一堆守卫** |
| `build_native_ui_loader.sh` | 编译 loader + guard + probe |
| `build_native_capture_probe.sh` | 编译照片观察器（需本地 `camera-mpr_monio.elf`） |
| `build_film_*.sh` | 编译其它组件 |
| `build_film_runtime_package.py` | 打包（只校验哈希，不补缺件） |
| `film-runtime-v2-templates/*.sh` | 冷启动 bootstrap / prepare / runtime-start |

### docs/（4 份说明）

`功能狀態.md`（实测到哪一步）、`編譯與目錄.md`（编译需要什么）、`發布檢查.md`（公开发布做了什么检查）、`授權說明.md`。

### 根目录

`README.md`（主说明）、`ACKNOWLEDGEMENTS.md`、`THIRD_PARTY.md`、`CONTRIBUTING.md`、`LICENSE`、
`test_film_*.py`（6 个 host 测试，共 156 项）。

---

## 三、★★★ 技术核心：它怎么让菜单出现在屏幕上

### 3.1 它用的是**索尼自己的 WidgetSystem**，不是自绘

`native_film_ui_trial3.c` 里解析了 **19 个索尼原生 API**（每个都校验 `指针 == BASE + 固定偏移`）：

```c
#define BASE 0x05d18000u
#define UI_SYSTEM ((void *)0x956f0)

WidgetC1Ev                              BASE+0x5ed5ad
GEN_PanelC1Ev                           BASE+0x4149ad
GEN_LabelC1Ev                           BASE+0x3c8d41
Widget::setLayerID(core::ID const&)     BASE+0x5ed50d
Widget::setLocalPos(core::Vector2 const&) BASE+0x5ed45d
GEN_Panel::setSize(float,float)         BASE+0x414def
GEN_Label::setSize(float,float)         BASE+0x3c9381
GEN_Label::setTextDirect(char const*)   BASE+0x3c88cf
GEN_Label::setFont(unsigned)            BASE+0x3c89af
GEN_Panel::setColorDirect(stella::CColor const&) BASE+0x4148e5
Layer::setActive(bool)                  BASE+0x5ef12f
Layer::setClear(bool)                   BASE+0x5ef159
WidgetBase::setAnimate(bool)            BASE+0x606951
LayerBase::moveLast()                   BASE+0x601331
core::ID::genID(char const*)            BASE+0x63dcd9
WidgetSystem::createLayer(core::ID const&) BASE+0x5f0f49
WidgetSystem::getMousePosInFramebuffer(unsigned char,short&,short&) BASE+0x5f1019
WidgetSystem::getMouseState(unsigned char,unsigned char) BASE+0x5f1045
InfraInput::GetPhyKeyStatusCache(int,_InfraInputPhyKeyData*) BASE+0x860fa9
```

**⇒ 它建的不是像素，是"索尼图层树里的一个 Layer"，里面塞 Panel + Label 控件。
  索尼的 UI 线程会照常把它栅格化 —— 所以"菜单有内容、能点"都是索尼提供的。**

### 3.2 建菜单的顺序（可直接照抄的结构）

```text
1. 校验 system 指针 == 0x956f0，tid == 当前线程
2. 校验 keys 单例 == 0x59150，且能读 0x1ab(MENU) / 0x70dc(放大鏡)
3. genID("A6400_FILM_...") → createLayer(system, &id) → 得到 ui.layer
4. layer.setActive(0); layer.setClear(1)
5. 建 1 个背景 Panel（0,0,640,480，色 {10,14,20,255}）
6. 建 6 个行 Panel + 8 个 Label
7. 每行：setLocalPos + setSize + setColorDirect + setTextDirect
8. 建一个 ticker Widget，**克隆它自己的 vtable**，把 slot 34 换成自己的 animate 函数
   ⇒ 这样就有每 15ms 的稳定回调（菜单的"心跳"）
9. ticker_layer = createLayer(...) → setLayer(ticker) → setActive(1)
10. setAnimate(ticker,1)
```

**关键技巧：`memcpy(ui.vtable, original-2, sizeof(ui.vtable))` 然后 `*(uintptr_t**)ui.ticker = ui.vtable+2`
—— 只克隆自己这个 Widget 的虚表，不碰全局。**

### 3.3 组合键与触控

```text
打开：MENU(0x1ab) + 放大鏡(0x70dc) 同时按住 ≥ 600ms
  且必须有"先松开过一次"的 latch（防止开机时就按着）
  且两次采样间隔 > 250ms 时丢弃 hold 时间（原生切屏会暂停回调）
关闭：触控点 Close 行 / 任何其它原生动作（22 个 dismiss key 之一）
触控：getMouseState 返回 5=按下 / 6=松开（**不是布尔**）
```

### 3.4 为什么它的菜单能"消失得干净"

```text
· 菜单是单独的 Layer，setActive(0) 就整体隐藏
· 它不拦截、不改写原生事件 —— 原生照常收到 MENU 和放大鏡
· trial 时限（30s~600s）到点自动 stop_trial()：hide + layer_active(0) + setAnimate(0)
· 还留了一个"外部停止位" a6400_ui_stop_requested_v1（0=idle/1=请求/2=已在 UI 线程停止）
```

---

## 四、它怎么把代码放进相机（这是最关键的部署模型）

### 4.1 三层结构

```text
① payload.bin（≤2296 字节，链接到固定地址 0x0700d708）
     = native_ui_load_payload.c + native_ui_trampoline.S
     作用：被写进 im.elf 的代码段，替换 0x06309d48 处 12 字节
② a6400-ui-load-guard.elf
     = 独立 PIE 程序，跑起来先 ptrace 冻结 im.elf 的全部线程（PID 149）
     然后往 /proc/149/mem 写：先写 payload 到 CODE=0x700d708，再打 12 字节跳板到 HOOK=0x6309d48
     写完恢复线程
③ a6400-film-ui-v3.so
     = 真正的菜单 so，放在 /tmp_bt/，由 payload 在"原生 release 回调"里 dlopen 并 attach
```

### 4.2 12 字节跳板（这是它能"只改 RAM"的关键）

```text
原 12 字节（native_ui_load_guard.c 里硬编码校验）：
  2d e9 f0 47 88 b0 4f f0 14 08 01 22      ← mt_original_release 的开头

替换成：
  df f8 xx xx f0 xx ...  bf 00  bf 00       ← 8 字节 ldr pc 跳板 + 2+2 填充
```

**⇒ 它把 `mt_original_release`（原生鼠标释放处理）的头 12 字节换成跳到 payload，
  而 payload 做的是"先调原函数，再检查是否满足注入条件，是则 dlopen 菜单 so"。**

### 4.3 一堆守卫（这部分最值得学）

```text
· maps_ok()：确认 /libObj.so 的 CODE 区间是 r-xp、STATE 区间是 rw-p
· 校验 HOOK 处 12 字节 == 原厂字节（或已 patch 的状态）
· 校验 /proc/149/comm == "im.elf\n"
· 校验 payload 的 FNV 哈希 == 请求头里的值
· freeze 前后枚举线程，确认数量一致
· 任何线程 PC 落在 HOOK+12 或 CODE+payload_len 内就放弃
· 失败路径 PTRACE_DETACH 全部线程
```

---

## 五、★ 移植到 ZV-E10 的可行性评估（逐条）

### 5.1 好消息：19 个 API 里 **18 个在我们 libObj 里都有**

我在本机 ZV-E10 原厂 libObj（`cfdc59b9…`）里逐个核对：

```text
✓ WidgetC1Ev                      0x613860
✓ GEN_PanelC1Ev                   0x164450
✓ GEN_LabelC1Ev                   0x115bb0
✓ Widget::setLayerID              0x6137c0
✓ Widget::setLocalPos             0x613710
✓ GEN_Panel::setSize              0x164892
✓ GEN_Label::setSize              0x1161f0
✓ GEN_Label::setTextDirect        0x11573e
✓ GEN_Label::setFont              0x11581e
✓ GEN_Panel::setColorDirect       0x164388
✓ Layer::setActive                0x6153e2
✓ Layer::setClear                 0x61540c
✓ WidgetBase::setAnimate          0x62cc08
✓ LayerBase::moveLast             0x6275e8
✓ core::ID::genID                 0x63cbbc
✓ WidgetSystem::createLayer       0x6171fc
✓ getMousePosInFramebuffer        0x6172cc
✓ getMouseState                   0x6172f8
✗ InfraInput::GetPhyKeyStatusCache   ← **我们这里没有这个符号**
```

**⇒ 18/19 齐备。缺的那个是"读物理按键缓存"，可以换别的方式（例如我们已有的
  WidgetSystem::processEvent + postMouse* 系列，或用 libInfraRemote 的按键注入）。**

### 5.2 但地址全部要重算

```text
A6400：BASE = 0x05d18000，UI_SYSTEM = 0x956f0，KEY_INSTANCE_SLOT = 0x0713dad0，keys 单例 = 0x59150
ZV-E10：libObj 里同一个函数是 0x616fe4（sys_getInstance），不是 0x5f0f49 系
  ⇒ 需要用「函数名 + 我们 libObj 的偏移」重建整张 API 表
  ⇒ UI_SYSTEM 指针、keys 单例地址都要重新找
```

### 5.3 更关键的差异：**我们的相机 A9 侧绘制路径有已知问题**

```text
· 我们之前实测 createLayer ×6 成功，但 layer 的绘制虚槽（libObj+0xdeaf4）是 3 指令空壳
· 而那正是 A6400 方案依赖的"由索尼 UI 线程把 Layer 栅格化"这条链
· 但 ZV-E10 与 A6400 是同一代 UI 架构（WidgetSystem/Layer/GEN_Panel/GEN_Label 都存在）
⇒ **需要先验证：我们在 QEMU 里 createLayer 成功了，真机上 layer 是否真的被栅格化
```

### 5.4 我们当前环境的约束（必须写清楚）

```text
· 相机 /usr 是 ro ext2，/tmp 与 /etc 共用 12 MB vfat（可写，且持久）
· 已有 TCP 7777 agent（root shell，但 RUN 依赖 /tmp 写入）
· 有 /log 500 MB 可写
· WBI 快照机制：改 RAM 的东西断电即失
⇒ A6400 方案**正好完全契合**：它只改 RAM + 只往 /tmp_bt 写 so
   而我们的 /tmp 就是 nflasha10（持久、可写）
```

---

## 六、可执行的移植方案（分四步，每步都可回滚）

### 步骤 0：先验证"原生 Layer 会被栅格化"（最高优先，纯只读）

```text
· 在真机上 dlopen libObj.so（RTLD_LAZY），dlsym 拿那 18 个函数
· 用 WidgetSystem::sys_getInstance() 拿 system 指针
· createLayer(system, genID("TESTLAYER")) → 看返回值是否非 NULL
· 若成功 ⇒ A6400 的整套机制在架构上成立
· 不改任何东西，纯探测
```

### 步骤 1：做一个"最小可见菜单"（只显示，不交互）

```text
· 一个 Layer + 一个背景 Panel(0,0,640,480) + 一个 Label("Hello ZV-E10")
· setActive(1) 后等 3 秒，再 setActive(0)
· 判据：屏幕上真的出现又消失
· 全程 RAM，断电即恢复
```

### 步骤 2：加心跳与组合键

```text
· 克隆一个 Widget 的 vtable，slot 34 换成 animate
· 用 getMouseState + 按键缓存检测 MENU + 放大鏡
· 判据：组合键能召唤、再按能关闭
```

### 步骤 3：接业务（LUT/Film 或我们要的功能）

```text
· 用 film_menu.c 的结构（它完全与相机解耦，可直接复用）
· 设置存 RAM（/tmp_bt/xxx.ini）
```

### 每一步的失败撤退

```text
· 全部是 RAM 操作 + /tmp 写文件
· 出问题：断电重启（RAM 清空）；或删 /tmp_bt/ 下的文件
· **不碰 /usr、不碰 /system、不碰 WBI**
```

---

## 七、不能确定的部分（不猜）

```text
? ZV-E10 的 WidgetSystem 的 UI 线程是否与 A6400 同样"每帧调用 animate 虚槽"
? 我们的 WidgetSystem::sys_getInstance 返回的单例，和 createLayer 接受的 system 指针是否同一个
? ZV-E10 的 keys 单例地址（A6400 是 0x59150）—— 需要运行时探测
? key 0x1ab / 0x70dc 在 ZV-E10 上是否仍是 MENU / 放大鏡
? 我们的 libObj 里 GEN_Panel/GEN_Label 的构造函数所需对象大小（A6400 用 0x200/0x400）
   —— 需要按 ZV-E10 的反汇编确认
? ZV-E10 上是否存在与 mt_original_release 等价的"每帧/每输入回调"可挂
   —— 这是整个方案的注入点，**必须先找到**
```

---

## 八、追加核实：我们这边的注入点比 A6400 更干净

### 8.1 A6400 的注入点（回顾）

```text
它必须把 mt_original_release 的头 12 字节换成跳板（改 im.elf 的 RAM 代码）
⇒ 因为 A6400 没有可注册的公开回调
```

### 8.2 ZV-E10 有 `InfraInput::SetCallback` —— 不用改代码字节

我在本机 ZV-E10 原厂 libObj 里核实到：

```text
_ZN10InfraInput11GetInstanceEv                0x8c22a0
_ZN10InfraInput11SetCallbackEPiPFviP16_InfraInputEventE   0x8c2824
_ZN10InfraInput10RmCallbackEi                 0x8c189c
_ZN10InfraInput15GetPhyKeyStatusEiP21_InfraInputPhyKeyData  0x8c199c
_ZN10InfraInput14GetEventStatusEiiP16_InfraInputEvent      0x8c2a00
_ZN10InfraInput14RmEventKeyListEiP18_InfraInputKeyListi    0x8c3454
_ZN10InfraInput15SetEventKeyListEiP18_InfraInputKeyListi   0x8c3540
_ZN10InfraInput15ReleaseInstanceEv           0x8c1810
_ZN10InfraInput17SendEventInitDataEi         0x8c2b28
_ZN10InfraInput9GetKeyInhEi                  0x8c2d0e
_ZN10InfraInput9SetKeyInhEib                 0x8c2e94
_ZN10InfraInput9SetOsalIdEPii                0x8c27b8
```

**⇒ `SetCallback(int*, PFviP16_InfraInputEvent)` 意味着我们可以：
  注册一个自己的按键回调，由原生输入服务每来一个事件就调一次。
  **完全不需要改任何代码字节、不需要 ptrace、不需要冻结线程。**

**这比 A6400 的跳板注入方式安全一个数量级。**

### 8.3 同时 A6400 缺的那个 API 我们也有

```text
A6400 用 InfraInput::GetPhyKeyStatusCache(int, _InfraInputPhyKeyData*)
ZV-E10 没有这个名字，但有 InfraInput::GetPhyKeyStatus(int, _InfraInputPhyKeyData*)
  ⇒ 功能等价，只是少了 "Cache" 的语义差别，需要实测确认
```

### 8.4 因此 ZV-E10 的移植方案可以简化

| 环节 | A6400 做法 | ZV-E10 可用的更简做法 |
|---|---|---|
| 拿到回调时机 | 改 mt_original_release 头 12 字节 + ptrace 写入 | **`InfraInput::SetCallback` 直接注册** |
| 版本校验 | 硬编码 BASE+offset 断言 | 同样可做（我们有符号名，dlsym 后比偏移） |
| 建菜单 | WidgetSystem::createLayer + Panel/Label | **完全相同，18/19 API 齐备** |
| 输入 | 读 GetPhyKeyStatusCache | `GetPhyKeyStatus` 或回调里直接收事件 |
| 退出 | trial 时限 + setActive(0) | 相同 |

**⇒ 我们的工作量主要在：**
```text
① 重建 API 偏移表（18 个符号，我们已有地址）
② 找到 WidgetSystem 单例与 UI 线程（createLayer 必须在 UI 线程调）
③ 确认 GEN_Panel / GEN_Label 的构造对象大小与属性表（A6400 用 0x200/0x400 + 6 个 uint32 属性）
④ 确认 key 码（MENU / 放大鏡）在 ZV-E10 上的值
```

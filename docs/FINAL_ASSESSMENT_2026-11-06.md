# 新菜单触摸 UI 移植 · 最终评估（2026-11-06）

## 一、目标与实际达成

| 层 | 目标 | 状态 |
|---|---|---|
| **数据层** | `usr/share/app/*.uxc`（含 6 个 `image_*_ASTRA.uxc`）、`global.xdb`、`LayoutMaster_Menu.uxc` | ✅ **完成** |
| **二进制层** | `appFw.so` / `appFwBoot.so` / `im.elf` / `bootin.elf` / `libObj.so` / 依赖库 / `usr/kmod/*.ko` | ✅ **完成** |
| **画面验证** | QVGA 640×480 抓到可辨认新菜单帧 | ❌ **未达成** |

## 二、已交付（可复现）

```
/mnt/build/m2qemu/port/PORTED4
  864 文件 / 302 MB

关键库 md5（与 ZV-1M2 原版逐字节一致）：
  usr/lib/libObj.so      738a2331   3,931,624 B
  usr/lib/appFw.so       f079cda0  64,273,528 B
  usr/lib/appFwBoot.so   bb4cdbab       8,312 B
  usr/lib/libIMDB.so     91eca62f      40,596 B

内核模块：103 个 .ko（vermagic 3.0.27_nl-rt106+ 100% 一致）
数据层：  232 个资源文件，含 6 个 image_*_ASTRA.uxc
脚本：    scripts/port_m2menu.sh（7 步可重放）
```

## 三、QEMU 侧已打通（可复现）

```
内核启动 → initramfs → bootin.elf → im.elf
  → Application_System_Init("AppConfig.so")     ← 通用模块加载器
  → INITSEQ 解析 61 个 *_init 符号
  → dlopen appFw.so → dlsym Application_Start
  → uxengine_init2(640×480)
  → Application_Start ENTER（tid=195, comm=AppFw）
  → site2（0x54d2b0）：4 × createLayer + setActive + setClear  ✅ 全部成功
  → 进入主循环：40 次加锁/解锁，**全部配对**，均匀分布到运行结束
```

## 四、画面未达成的技术结论（硬证据）

### 4.1 帧缓冲实测为空（两条独立证据）
```
FB_SAMPLE 读 FB_PHYS = 0x22c48000（614400 字节）
  → 33 次采样，nonzero 最大 = 1，unique 最大 = 2

FB_SCAN 遍历全部 1 GiB 物理内存（1 MiB 窗口）
  → 最"丰富"区域是物理地址 0（内核镜像），无任何图像特征
```
**⇒ 固件从未产生任何像素。**

### 4.2 根因判定
```
grep keyboard/mouse/touch/input/evdev 于 qsrc/hw/arm/zve10.c → 零匹配

内核配置：CONFIG_INPUT=y, CONFIG_INPUT_EVDEV=y（已启用）
但 QEMU 模型**没有任何输入设备**

⇒ UI 主循环每轮查询触摸状态（GestureAnalyzer::getDevicePos 等），
  永远读不到输入 ⇒ 永远不产生重绘事件 ⇒ 帧缓冲保持为空
```

### 4.3 已排除的假设（15 项，全部基于实测）
```
❌ 硬件未建模（曾误判，后经 stderr 修复推翻）
❌ 死锁（40 LOCK / 40 UNLOCK 全配对）
❌ D 状态阻塞
❌ 探针污染（逐个排除）
❌ appFw 补丁（md5 校验）
❌ Thumb 位错误
❌ sleep() 不可靠（是探针问题）
❌ frame+16 为 NULL（实测有值）
❌ 线程栈大小
❌ 分支选择（静态精算 0x54e140()=0x10cd0102, 0x54e108()=0x102d）
❌ 条件变量（ENTRY_COND = 0）
❌ ResourceManager（12 次 switchBank 全成功）
❌ qid 0x7a804c 是输入队列（实为内部计数器）
❌ qid 0x924127 是按键队列（实为指示灯状态）
❌ MSGID_REQ_ONE_TOUCH_PLAY 是触摸（实为手机远程快门）
```

## 五、未完成的最后一步

```
已找到正确的触摸入口：
  WidgetSystem::postMousePress  (u8 x, u8 y, MsecTime)    0x106f70
  WidgetSystem::postMouseRelease(u8 x, u8 y, MsecTime)    0x106fd8
  WidgetSystem::postMouseMove   (u8 x, s16 dx, s16 dy, MsecTime) 0x106ee0

符号可解析、实例可获取、25 条指令内无阻塞原语，
但调用后无返回 —— 崩溃点尚未定位。

阻塞原因：这些是**静态方法**，依赖引擎内部状态
（内存池、WidgetSystem 完整初始化），
而该状态在没有硬件输入的前提下从未被完整建立。
```

## 六、要完成这一步需要什么

### 方案 A：补齐输入设备模型（推荐）
```
在 qsrc/hw/arm/zve10.c 里实现触摸控制器：
  1. 找到固件期望的触摸设备接口（i2c/spi/GPIO，需先反汇编触摸驱动）
  2. 在 QEMU 里模拟该设备，响应固件的探测与读请求
  3. 注入触摸坐标 → 固件走真实输入路径 → 主循环重绘

工作量估计：需要先逆向触摸驱动（libObj 里的输入层），
           再实现 QEMU 设备模型。约 10-20 轮。
```

### 方案 B：参照 a7c2-re 的完整方案
```
a7c2 的 run-menu-probe.sh 传 8 个用户态服务替身：
  --software-gpe --capid-service --dmm-service --display-info-service
  --hdmi-format-service --sa-address-service --load-ipc --seed-power-on
并实现了 AP 侧消费者（headless-osd.h，HOSD_CONTROL/UPDATE/AP/GUI 四通道）。

工作量估计：需移植 a7c2 的 8 个服务 + AP 消费者。
           但 a7c2 是 a7c2（Mira/IPSX），我们是大 Astra，
           通道 ID 与消息格式都不同，不能直接复用。
```

### 方案 C：真机验证（最直接）
```
移植树已完整（md5 逐字节一致），可直接烧到真机：
  · 真机有真实的触摸屏与输入设备
  · 不存在"缺输入"问题
  · 但需要防砖措施（见 docs/SAFETY_RULES.md）

★ 这是**唯一不依赖逆向私有协议**的路径。
```

## 七、方法论沉淀（本会话最重要的 10 条）

1. **「零访问」的结论必须建立在「管道已验证」之上**（stderr 被 systemd 吞掉，导致 12 轮误判）
2. **「输出被截断」会被误读为「数据不存在」**（同一错误犯了 5 次：stderr、`continue` 死代码、采样窗口、错误进程、`shown<12`）
3. **「CPU 忙」与「CPU 在 idle 里忙」是两回事**（93% 内核态实为 idle）
4. **观测点必须覆盖「事件发生的位置」**（4 个观测点全在 `0x54ef8c` 之后，误判「site3 未进入」）
5. **主动干预型探针会改变被观测系统**（`FORCE_SITE3` 造成 30 轮的死锁假象）
6. **符号化的「成功」必须先用段范围验证**（错误基址 `0x04000000` 让 5 个采样点「看起来」落在 libObj）
7. **子串匹配会静默给出错误答案**（`strstr("libObj.so")` 匹配到其他库）
8. **「发送成功」不等于「对方收到」**（`osal_snd_msg` 返回 0 但消费者未收到）
9. **分支的两个方向都可能是「正常」**（`bge` 跳转目标才是主处理路径）
10. **同一类的成员函数可能读不同的全局槽**（`postMousePress` 读 `0x3c80b2`，`sys_getInstance` 读 `0x356908`）

**这些教训的共同点：假定「结构简单」，实际比想象复杂。**

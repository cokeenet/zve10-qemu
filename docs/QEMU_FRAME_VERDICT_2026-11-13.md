# QEMU 出帧路线的完整结论（最终版，goal round 275）

> 本文只记录 QEMU 侧（`-M zve10` 机器模型）的实测结论。
> 真机路线见 `MASTER_HANDOVER_NEWMENU.md`，两者不要混淆。
> 所有数字都来自 `/mnt/build/m2qemu/out/v*/` 下的真实运行日志。
> **本文已剔除早期被"测量污染"的结论**（见第九节），只保留经多次交叉验证的事实。

---

## 一、一句话结论

**Gen5 固件在 QEMU 里的启动链完整跑通、UI 的 layer 树完整建立，
但它**从未进入自己的绘制循环**，因此一个像素都没有产生。**

**"为什么不进入绘制循环"这一点，在本会话内未能确定** ——
原因是那道判据函数（`appFw+0x54f2ca`）既不是导出符号，
其指令窗口（4+2+4=10 字节）也无法安全 inline hook。**观测路线已到尽头。**

---

## 二、已确证的事实（每条都有证据来源）

| # | 事实 | 证据 | 交叉验证 |
|---|---|---|---|
| 1 | 启动链全通：bootin → appFwBoot → im.elf → appFw | 四层 sha256 匹配、2730 个构造函数执行 | 多轮 |
| 2 | `createLayer` 调用 **6 次，全部返回非 NULL** | `CREATELAYER n=1..6` + 返回值 | v303/v310/v312/v314 |
| 3 | layer 3 使用固件自己生成的 ID（`0x8001653f` = GENIDPROBE AAA） | `CREATELAYER n=3 id=0x313b6e00 w=0x8001653f` | v303 |
| 4 | `uxengine_init2` 执行并**返回非 0** | `INIT2PROBE enter/return r=0x4cc0c0` | v303/v310/v314 |
| 5 | `uxengine_init2` 第一行就设置 640×480 | `0xe281a mov.w r1,#640` / `0xe2822 mov.w r2,#480` | 反汇编 |
| 6 | `GFXENG` 被进入并返回 | `GFXENG enter n=5 → leave n=5 ret=0x4cc0c0` | v303 |
| 7 | 画布已映射，大小恰好 614400 字节 | `MMAPOBS off=0x22d52000 len=0x96000` | 多次 |
| 8 | **画布读数恒为零**（前 2 KB，6 次采样） | `CANVASX/CANVASY nz_words_512=0`（运行时跟踪真实 VA） | v296 |
| 9 | `updateFrame` 的 PLT 槽 **never-called** | `GOTS updateFrame ... PLT0(never-called)` | v303 |
| 10 | `isFrameDelay` 探针 **0 命中** | 无污染基线，interpose ABI 正确 | v303/v311 |
| 11 | `updateCurrentTime`（主循环入口必经）**0 命中** | 无污染基线 | v303 |
| 12 | GPE 只有 **19 个启动 fence，无任何绘制包** | `ZVE10_GPE_PACKET` ×19 内容相同；`ZVE10_GPE_OTHER=0` | v303/v314 |
| 13 | `av-cam.bin`（MUSASHI 17 MB）存在时主流程停在 **t=120** | `COUNTERS-DONE` | v305 |
| 14 | 同一配置下移除 `av-cam.bin` 后主流程健康到 **t=1125**、`createLayer=6` | `COUNTERS-DONE` + 基线哨兵 | v306 |

**⇒ 第 13/14 条是单变量对照实验：唯一变量就是 `av-cam.bin` 是否存在。**

---

## 三、绘制链的结构（反汇编证据）

```
appFw 0x54ed12  主循环函数入口
  0x54ed22  blx  WidgetSystem::updateCurrentTime()
  0x54ed36  blx  WidgetSystem::processEvent()
  0x54ed50  blx  WidgetSystem::isFrameDelay()
  0x54ed54  cbz  r0, 0x54ed90          ← 为 0 就跳出（不画）
  0x54ed5a  blx  WidgetSystem::updateFrame()   ← 画
```

**而进入该循环的唯一调用点：**

```
appFw 0x54faae  bl 0x54ed12           ← 全 appFw 仅此一处
```

**其前方有两道判断，其中真正会导致跳过的是：**

```
0x54fa3c  bl  0x54f2ca
0x54fa40  cmp r0, #2
0x54fa44  beq 0x54faf0                ← 返回 2 就跳过主循环
```

**而 `0x54f2ca` 内部：**

```
0x54f2fa  ldr.w r0, [r5, #0x870]
0x54f2fe  cmp   r0, #2
0x54f302  movgt r0, #2                ← >2 返回 2
0x54f304  movle r0, #0
```

---

## 四、为什么这条链无法继续观测（重要）

**`0x54f2ca` 不是导出符号** —— 它落在 `Application_Start`（0x54d580）内部，
最近的导出符号就是 `Application_Start` 本身。因此：

```
✗ 符号级 interpose：不可能（符号不存在）
✗ inline hook：不可能（0x54ed50..0x54ed58 = 4+2+4 = 10 字节，
                        8 字节 trampoline 会切断 0x54ed56 的 blx）
```

**⇒ "读那道门"这条路在观测层面已经走到尽头。**

---

## 五、已排除的假设（每条都有测量依据）

| 假设 | 如何排除 |
|---|---|
| 缺 `/dev` 设备 | `/dev/mem` 打开成功（DEVOPEN ret≥0） |
| mmap 失败 | 29 次 mmap **0 次失败** |
| 参数不对（108 组组合） | 全返回 -9999，但该失败码所属路径固件根本不走 |
| 我们注入造成污染 | 关注入后 `0xe1700` 仍出现 4 次 |
| dmm.ko 是队列接收方 | dmm 用的字面量是 **0x8f013a** |
| 是"硬件状态轮询" | 映射者是 dmp 分配器（普通内存分配） |
| 缺未加载的 Infra 库 | 引用库都在部署树里，但进程只加载 3 个 |
| 需要第二个 CPU 核 | Gen5 无双核负载；`-smp 2` 已知必 panic（参考文档亦证） |
| 缺 `dma330_stub` | 加载后 liro 确实能起来（见第六节） |
| dma330 真驱动导致挂起 | 用 stub 同样挂起（v305） |
| `[self+0x870]` 是忙计数 | **两次运行该值不同且是可打印 ASCII 残留 ⇒ 证伪** |
| 缺软件渲染后端 | 固件连绘制请求都没发出，谈后端为时过早 |

---

## 六、`av-cam.bin` 与 MUSASHI 的关键发现

```
liro.ko 是 **uITRON RTOS + MUSASHI 固件加载器**：
  · 加载 /system/av-cam.bin（17,195,164 B）到 0x635c6000
  · 创建 2 个内核线程（kthread_create_on_node）
  · 把 RTOS 任务优先级设为 5（os_tsk_setprio）
  · 固件确实在同一 A9 核上执行（737 个 PC 样本落在 0x635c6000..0x6462c69c，
    全部为唯一地址 ⇒ 不是死循环，是真在工作）
```

**但它启动后主流程立刻停住（t=120 vs 移除后的 t=1125）**
**⇒ 在单核上，MUSASHI 主循环与 UI 主流程互斥。**

**补充：参考树 `ref-chocotan-zve10/work/initramfs-ui/system/` 是空目录**
**⇒ 参考实现根本没有 MUSASHI 固件可加载，所以它的 liro 加载后无事可做、主流程照常。**
**而参考的帧来自 Gen4 自己的软件渲染路径，与 MUSASHI 无关。**

---

## 七、参考实现的路线（已读透，但对 Gen5 不适用）

`sonyfw-zve10-qemu` 能出帧靠三块：

```
① GPU 包应答极简：只完成那一个捕获到的启动 fence，其余不应答
② 软件渲染后端：把 Gemini 的 solid/rect/quad/line 整体替换成 CPU 光栅化
③ present 捕获 + tile 重排：8×8 瓦片 → 线性帧（640*480*2 = 614400）
```

**但它是"运行 ZV-E10 自己的 Gen4 固件"（libObj = cfdc59b9…，无 appFw），
而我跑的是 Gen5（libObj = 4cd654f1… + appFw = f08c9eb7）。**

**⇒ 参考证明的是"模型够用"，不能证明"Gen5 也能在 Gen4 模型上画"。**
**按 prologue 字节匹配，参考的 12 个 hook 有 9 个能在我们的 libObj 里定位，
但"能定位"不等于"语义相同"。**

`a7c2-qemu` 走的是全硬件模拟路线（虚拟传感器/镜头/编码器），取景至今仍黑，
**对"让 Gen5 菜单显示"这个目标不是可复用路径。**

---

## 八、结论：在什么条件下这个目标可以实现

```
① 多核：让 MUSASHI 任务跑在另一个核上
   ⇒ 但 -smp 2 已知必 panic（CPU1 执行即内存覆写，参考 docs/smp-investigation.md 同结论）
   ⇒ 需要先修 QEMU 的 per-CPU 本地定时器路径

② 或让 MUSASHI 固件在等待时主动让出 CPU（它没有这么做）

③ 或走真机验证（不依赖 QEMU）
```

**⇒ 在本会话的条件下（单核 QEMU + 未修改的 Gen5 固件），这三条都不可行。**

---

## 九、测量污染的教训（本会话最大的方法收获）

**以下结论曾被得出，后被自己的复查推翻，记录在此防止后来者重蹈：**

| 曾得出的（错误）结论 | 推翻方式 |
|---|---|
| "绘制函数被真正调用过（draw_entry 10 次）" | 那些调用是**我自己 DRIVE_NOTIFY=1 触发的** |
| "GRM_bitmapCreate 被调用 24 次" | 全在 tid=195，是**我的 GCREATE 探针自调用** |
| "notifyDraw 从未返回 ⇒ 卡死在 OSAL" | 窗口在主线程停后仅差 6 行到期 ⇒ **窗口伪影** |
| "INITSEQ 走到第 62 项后停住" | INITSEQ 只是 **dlsym 名称解析**，与执行无关 |
| "`[self+0x870]` 是 >2 的忙计数" | **两次运行值不同，且是可打印 ASCII 残留** |
| "stage1 转发到渲染容器" | 该指针解析后落在 **libUsbBusPower.so** |

**由此形成的纪律（建议沿用）：**
```
· "没有观察到" ≠ "不存在" —— 先证明测量能看到非零
· 任何"从未/为零"的结论，都要检查窗口边界
· 任何单次读数，都要重跑一次确认可复现
· 加观测手段前，先确认它的 ABI/语义与被观测函数完全一致
· 自己发起的调用必须与固件自身行为严格区分（按 tid / caller 过滤）
```

---

## 十、状态与可复现命令

```text
QEMU 实例   0
可用基线    v303 / v306 / v310 / v314（均 createLayer=6 / LIBOBJ_SYM=24）
最新基线    v314: 5586 行 / createLayer=6 / LIBOBJ_SYM=24
账本        /root/fw/oss/docs/QEMU_M2_PROGRESS.md（680+ 章，含每步细节与纠错记录）
```

复现干净基线（纯观测，无任何自驱动/注入）：

```bash
cd /mnt/build/m2qemu
# init 第 44 行为：
#   FORCE_MENU=0 DRIVE_NOTIFY=0 NO_DRIVE=1 INJECT_INPUT_QUEUES=0 INJECT_CP=0 FORCEDRAW=0
WINDOW=600 ./run-wlist.sh
```

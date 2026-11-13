# QEMU 出帧路线的完整结论（goal round 227）

> 本文只记录 QEMU 侧（`-M zve10` 机器模型）的实测结论。
> 真机路线见 `MASTER_HANDOVER_NEWMENU.md`，两者不要混淆。
> 所有数字都来自 `/mnt/build/m2qemu/out/v*/` 下的真实运行日志，不是推断。

---

## 一、一句话结论

**启动链在 QEMU 里完整跑通，绘制函数被真正调用过，但一个像素都没画出来。**
**卡点不在"缺硬件模型"，而在"Gen5 固件的 UI 初始化在一个事件循环里出不来"。**

---

## 二、做通了什么（全部有日志证据）

| 阶段 | 证据 | 结果 |
|---|---|---|
| bootin → appFwBoot → im.elf → appFw | 全链 sha256 与 Gen5 一致 | ✓ |
| appFw dlopen + 2730 个构造函数 | `Application_Start` 进入 | ✓ |
| `createLayer` | `CREATELAYER n=6` | ✓ |
| `updateFrame` | 5 个 panel 走完 | ✓ |
| `drawFramebuffer` | 执行并返回 | ✓ |
| `notifyDraw`（widget 系统自身入口） | 被真正调用（v243） | ✓ |
| widget 链表遍历 | 循环体命中 6 次（v244） | ✓ |
| `draw_entry`（`0x105f98`，Gen5 绘制函数） | 命中 **10 次**，每次收到合法节点指针（v251） | ✓ |
| `GetAllocatedRectangle`（`0x114e78`） | 命中 | ✓ |
| 我们自己 vtable 的槽 6 | 被 Gen5 真实调用（`GBMINVT n=1`） | ✓ |

**基线哨兵**：`createLayer=6`、`LIBOBJ_SYM=24`、行数 5700–6100。

---

## 三、没做到什么

```text
✗ 判据 2：640×480 / 614400 字节 / 非零 >1000 —— 显示窗口 128 MiB 全扫 best_nonzero16 = 0
✗ 判据 3：无可复现哈希（没有帧）
✗ 判据 4：键注入入队但从未被消费（no RCVC_RET with bytes=ab01）
✗ 判据 5：无业务行为可验证
```

**并且**不是没找过：`0x22d52000` 那块 present 目标缓冲、以及 128 MiB 整个显示窗口，
**在整轮运行中始终为全零**（由 QEMU 定时器每 3 秒采样，不依赖 atexit）。

---

## 四、卡点的精确描述

### 4.1 UI 初始化线程陷入循环

```
0x1706085 上 tmo=-1 永久等待 → 收到 type=0xe1700（不是它等的 0x61003c）
→ 丢弃 → 再发请求到 0x61003c → 再等 → 再收 0xe1700 → 循环
```

实测数字（v280 / v287）：

```
发送目标队列 = 0x61003c     （SNDTYPE 打印，4 次一致）
等待的队列   = 0x1706085    （由 osal_dynamic_id(42, 0x170, &out) 产生）
期望消息类型 = 0x61003c
实收消息类型 = 0xe1700     （f4 递增 0x405→0x10406→0x20407→0x30408）
```

### 4.2 三条独立证据表明 0x61003c 没有接收方

```
SND_DRIVE id=0x61003c = 4 次（发出去了）
RCVC qid=0x61003c     = 0 次（没人在收）
REG  qid=0x61003c     = 0 次（从未注册）
```

### 4.3 已排除的解释（每条都有测量）

| 假设 | 如何排除 |
|---|---|
| 缺 `/dev` 设备 | `/dev/mem` 打开成功（10 条 DEVOPEN ret≥0） |
| mmap 失败 | 29 次 mmap **0 次失败** |
| 参数不对 | 108 组 fmt×yuv×short 组合全返回 -9999，但失败码来源已查清 |
| 我们注入造成污染 | 关掉全部注入（`INJECT_*=0`）后 `0xe1700` 仍出现 4 次 |
| 是 dmm.ko 的队列 | dmm 用的字面量是 **0x8f013a**，不是 0x61003c |
| 是"硬件状态轮询" | 映射者是 dmp 分配器（`0x120ae2`），是普通内存分配 |
| 缺未加载的 Infra 库 | 引用 0x61003c 的库都在部署树里，但进程只加载了 3 个 |

---

## 五、`-9999` 的完整来源链（这一条已经查到底）

```
BMP_Create(0x1191c0)
  → 0x11ba1c（真实主体）
    → 0x12aee4（分配 helper）
      → 0x12a238
        → 检查 [self+0x10] == 16，否则返回 **0xffffd8f1 = -9999**
0x129aa0 才是把 state 从 1 推到 16 的那个函数（构造 0x129a30 设的是 1）
```

**实测**：`statedrive`（0x129aa0）命中 **0 次**、`alloc_gate`（0x12a238）命中 **0 次**。
**⇒ 这两个函数在正常流程里从未被调用，所以 state 永远停在 1。**

**注意**：我们曾试着直接调 `0x129a30`(构造) → `0x129aa0`(推进)：
构造成功（state=0→1），但推进调用**卡住不返回**（v274），因此直调路线已放弃。

---

## 六、两个"不能 preload 覆盖"的符号（重要教训）

```
✗ osal_reg_sync_msg_queue_cb    ⇒ 覆盖它，启动崩（1779 行）
✗ IMDB_find_entry               ⇒ 覆盖它，启动崩（538 行，纯透传也崩）
```

**规律**：启动极早期被解析的符号，一旦被 preload 对象导出，动态链接器的
解析路径就变了，im.elf 的模块装载直接失败。
**⇒ 这类符号只能用"只读观测库内部调用点"，不能替换符号本身。**

---

## 七、QEMU 侧要继续需要补什么（如果将来继续）

按可行性排序：

```
1. 找到 0x61003c 的真实注册者（它不在已部署的库、也不在 dmm.ko）
   —— 目前 REG 表 49 条里没有它，说明注册代码路径从未执行
2. 该注册者若在某内核模块（osal_ulogio / usbg_stillimage 等含 0x61003c 的模块），
   检查它们的 init 是否也像 dmm 那样**忽略初始化返回值**
3. 让 vdf 的 state 推进（0x129aa0）被执行 —— 它只被 rsrc_port_init 内部调用
```

**但必须诚实说明**：即便以上都过了，后面还有"画布分配 → 显示提交"两步从未触达。
**因此我不建议继续在 QEMU 内逐层破译**，除非有新的外部信息。

---

## 八、真机路线的正确做法（与 QEMU 无关，可独立验证）

**形态 B：loop + bind（零风险，重启即恢复，不碰 `/usr`）**

```bash
python3 /root/fw/oss/tools/bindm2/deploy_bindm2.py --image /mnt/build/nflasha15_v9b3b.ext2
```

三项 preload（缺一不可）：

```
/usr/tool/libefence.so : /log/libIMDB_m2.so : /log/bindm2.so
        ↑ 白名单占位      ↑ 顶替 DT_NEEDED      ↑ 构造函数里 loop+bind
```

主机侧已验证的能力（本轮会话新增）：

```text
✓ 设备 glibc 2.11.2 的 dlmopen 可用（**必须 RTLD_LOCAL，不能 RTLD_GLOBAL**）
✓ 两代 libObj 可在同一进程的两个 link namespace 里共存（lmid=1 实测）
✗ RTLD_NOW 会因 libMWF 的 osal_wait_thread_exit 未定义而失败 ⇒ 只能 RTLD_LAZY
```

**⇒ 若要做"新老 UI 并存"（形态 A），技术上成立，但需要一层显示仲裁，
而那一层的依据恰好需要 QEMU 侧先搞清楚 Gen5 怎么画 —— 这就是两条路的分歧点。**

---

## 九、状态与约束

```text
QEMU 实例   0
最新基线    v287: 5760 行 / createLayer=6 / LIBOBJ_SYM=24
账本        /root/fw/oss/docs/QEMU_M2_PROGRESS.md（第六百二十章，共 620+ 章）
```

**验证过的"测量手段失效"清单（防止后来者重蹈）**：

```text
· atexit 在 SIGTERM 下不执行 ⇒ 用 QEMU 定时器
· PC 采样只覆盖内核态（30040 个样本全在内核地址）⇒ 不能用来判断用户态
· tick 标签曾写成 n*5，实际每圈 1 秒 ⇒ 时间换算要按实测
· 8 字节 inline hook 窗口必须落在指令边界（切到 bl 中间会让循环次数失真）
· 探针密度会显著降低可达率 ⇒ 探针要少、要一次只改一个变量
· 探针绝不能在该上下文里调用固件代码（会重入状态机）
```

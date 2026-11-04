# GPE / grm_gles.ko 贡献包

本包整理 `/dev/dmpgles2` 图形设备的 ioctl 语义与 QEMU 设备模型补丁，供参考。
包含 `grm_gles.ko` **全 17 条 ioctl 注解表** 与 **GPE 设备模型补丁**。

> ⚠️ **分发说明**：本目录**只有原创分析文本和我们自己写的代码**，
> **不含任何 Sony 固件、二进制、证书或副本** —— 不含 `grm_gles.ko`、不含 `.bin` dump、
> 不含帧缓冲截图；反汇编片段仅为说明所需的最小引用。许可与 PolyForm Noncommercial 1.0.0 兼容。

---

## 文件清单

| 文件 | 内容 |
|---|---|
| `GRM_GLES_IOCTL_TABLE.md` | `/dev/dmpgles2` 全 17 条 ioctl 逐条注解：命令号、`_IOC` 字段、用户态调用点、所在函数、驱动分支地址、语义、是否阻塞、唤醒机制；含两路独立推导的交叉验证与运行时证据（747 行） |
| `GPE_MODEL_PATCH.md` | 中断面分析（`sugilite_interrupt`、`bar+0x34` 位定义、ack 寄存器、用户态门铃），**附约 60 行可直接合入的 `bionz.c` 设备模型补丁** |
| `GPE_WINDOW_DESIGN.md` | `0xF1000000` 那 16KB 寄存器窗口的建模设计笔记 |
| `probe_dmpgles_driver.sh`、`dis_ioctl.sh`、`ioctl_trace.*`、`scan_ioctl*.py` | 我们自己的探针 / 反汇编 / ioctl 枚举工具 |

---

## 针对上游卡点的七条

上游 README 把当前阻塞描述为：图形驱动在 **IRQ 76** 注册线程化中断、在 **`bar+0x34`** 读状态；
中断能送达、fence 包能提交，但**等完成位的 ioctl 永不返回**。我们表里对应的关键结论：

1. **完成判定**。`ioctl 0xC0048203` 通过 `prepare_to_wait` + `schedule_timeout`
   （最多 10 轮）等 `dev->[0xa8] & mask`；驱动用 `tst(arg & 0xC4000000, dev->a8)` 判定，
   命中后用 `bic` 清位。
2. **`a8` 从哪来**。`sugilite_interrupt` 累加
   `dev->a8 |= (st & 0xFFFF0000) | (read(bar+0x34) & 0xFFFF0000)`。
   **模型必须在 ack 之前把状态高 16 位摆到 `bar+0x34`** —— 只抬中断不够，
   这正对应「中断送达但 ioctl 不返回」。
3. **掩码写死在用户态**。`libObj` 固定用 bit26
   （`0x04000000`，`mov.w r2,#0x4000000` @ `libObj+0x12cc2c`）。
4. **只有 `__wake_up`，从不 `complete_all`**。ISR 只调 `__wake_up(dev+0x4C, 1, 1, NULL)`。
   completion 属于**另一对**：`0xC004820D` 等（`10^7` jiffies ≈ 27 小时，等价永久），
   `0xC004820E` 调 `complete_all`。把中断等待建成 completion 就永远唤不醒 `0xC0048203`。
5. **顺序不能反**。门铃 `bar+0x1800`（唯一 kick 点，`0x1804` 只存不抬）：
   先 `int_status |= 0x04000000`，**再** `qemu_set_irq(irq, 1)`。
   ISR 开头是 `if (st == 0) BUG()`，抬线早于状态可见会触发内核 BUG。
6. **ack 按写入值判定**。`bar+0x1C` 双用途：init 写 `0x10000` 是配置，ISR 写 `0` 才是 ack；
   `0xC18 = 0` 和 `0x3C/0x44 = 1` 是另外两种 ack 形式。别只看偏移。
7. **IRQ 数组下标**。`cxd90045_init()` 里用 `irq[76 - 32]` = `irq[44]`，不是 `irq[76]`。

补充：**2D 命令不走 ioctl**。`libObj` 用 `0xC0048202` / `0x80048206` 取得
`phys=0xF1000000, size=0x4000`，经 `/dev/mem` mmap 后直接 MMIO 写。

---

## 我们这边的验证状态

- `app.err` 出现 `===== DMP_2D_Initialize : 0` —— 该调用确实返回。
- GPEW 日志：42 次写（`0x1800`×8 / `0x1804`×8），**8 次抬线 / 8 次 ack，严格 1:1**，
  另有一次 init 期的 `0xC18 = 0`；无中断风暴。
- 静态核对：`sugilite_register_init` 的 34 个写偏移里没有 `0x1800` / `0x1804`
  （开机不会误抬线）；一次抬线对应恰好一次 ack。

未闭环的项在 `GRM_GLES_IOCTL_TABLE.md` §5 单列，否定结果也一并保留在案。

---

## 适用范围

这是从 **ZV-1F 供体树**、自研 `cxd90045` QEMU 机型上做的独立分析。
上游的 ZV-1M2 / ZV-E10 树生成地址可能不同；上面的命令号、位语义、
`wake_up` / `completion` 分工是可移植的部分。

# GPE（Sugilite）窗口建模 Spec —— 让 DMP 初始化拿到"完成信号"

> 目的：给 QEMU `bionz.c` 加一块极简的 GPE/Sugilite 寄存器窗口，使 App 的
> `DMP_2D_Initialize` 能走完一次"发命令 → 中断 → 唤醒"的回合。
> 证据全部来自 `grm_gles.ko`（Sony 原厂驱动）与 KLOG / App maps 实测。

---

## 1. 已确认的硬件事实

| 项目 | 值 | 证据 |
|---|---|---|
| 驱动 | `/usr/kmod/grm_gles.ko`（15,648 B） | 目录；module 名 `grm_gles` |
| 自述 | `description=SUGILITE Driver`，`author=DMP/Sony`，`version=0.6` | `.modinfo` |
| 源码文件 | `.../Guiengine_161H/driver/grm_gles/src/sugi_hw.c`、`sugi_dev.c` | 内嵌路径字符串 |
| 设备名 | `dmpgles2` → `/dev/dmpgles2` | 字符串 + App 已 open |
| BAR | **物理 `0xf1000000`，大小 `0x4000`** | KLOG：`!!! GPE HW: bar remapped 0xf1000000->0xf1000000 size 4000`；App maps：`0409b000-0409f000 rw-s f1000000 /dev/mem` |
| 中断 | **Linux IRQ 76**，handler 名 `dmpgles2` | 进程表线程 `irq/76-dmpgles2` |
| 访问方式 | App **mmap BAR 后直接读写寄存器**（不走 ioctl） | 驱动导出 `sugilite_mmap`；App maps 有 `rw-s f1000000` |

## 2. ISR 逻辑（`sugilite_interrupt`，.ko 内 `0x640`，ARM 模式）

```c
bar = dev->bar_base;                        /* dev->[0xf8] */
st  = ioread32(bar + 0x34);                 /* ★ 中断状态 */
if (st == 0) BUG();                         /* ★★ 状态为 0 却进中断 = 内核 BUG */
if (st & 0x80000000) iowrite32(0, bar + 0x1000);   /* 清主中断 */
if (st & 0x40000000) iowrite32(0, bar + 0xC18);
if (st & 0x04000000) iowrite32(0, bar + 0x1C);
if (st & 0x0000000F) { iowrite32(1, bar + 0x3C); iowrite32(1, bar + 0x44); }
st2 = ioread32(bar + 0x34);                 /* 再读一次，取高 16 位 */
dev->[0xa8] |= (st & 0x0000FFFF) | (st2 & 0xFFFF0000);
__wake_up(dev + 0x4C, 1, 1, NULL);          /* ★ 唤醒等待队列 */
return IRQ_HANDLED;
```

驱动还 import 了 `osal_snd_msg` / `osal_snd_sync_direct` / `complete_all` / `down_interruptible`
⇒ 唤醒之后，由驱动的上下文**通过 uipc 消息**通知 App；App 侧再 signal 它自己的 condvar。

## 3. 建模要点（最小实现）

在 `hw/arm/bionz.c` 里新增一个 0x4000 的 MMIO 区域：

1. **寄存器存储**：`uint32_t regs[0x4000/4]`，reset 为 0。
2. **读**：`bar + 0x34` 返回 status 寄存器；其余按存储返回。
3. **写**：
   - 写到 `0x3C` / `0x44` / `0x1C` / `0xC18` / `0x1000` 时，按位清掉 status 对应位（见 §2 的掩码）；
   - 写到**命令寄存器**（待定，见 §5）时：先把 status 置成非 0（建议 `0x1`，即低 4 位之一），
     **再** raise IRQ 76。
4. **IRQ**：用 SoC 的 `qdev_get_gpio_in(dev, <line>)` 取到对应线，`qemu_irq_raise` / `qemu_irq_lower`。
   ⚠ `<line>` 的取值待确认（Linux IRQ 76 到 INTC 线号的映射）。

**为什么必须先置 status 再抬中断**：ISR 第一件事就是读 `+0x34`，读到 0 会直接 `__bug_noarg`
（内核 BUG 路径），比现在的死等更糟。

## 4. 验证方法

加日志（`qemu_log_mask(LOG_UNIMP | LOG_GUEST_ERROR, ...)` 或临时 `fprintf`）记录：
- 每次访问的 `offset / size / 是读还是写 / 值`
- 每次 raise IRQ 的时机

跑一轮（`work/run_diag11.sh` 那套），然后看：
1. App 到底有没有访问这块窗口（**如果一次都没访问，说明 DMP 卡在更早的地方**，本方案不成立）；
2. `app.err` 里 `===== DMP_2D_Initialize : N` 有没有出现（出现即代表 DMP 初始化返回了）。

## 5. 未确定项（施工前需要补）

- [ ] **命令寄存器是哪个偏移**：App 拿 BAR 是直接写寄存器，静态上不好定位；
      只能靠"先 log 所有写操作"实测出来（见 §4 第 1 步）。
- [ ] **Linux IRQ 76 → 模型 INTC 线号**：`bionz.c` 里是 `irq[32][16]` + `qdev_get_gpio_in(dev, ch*16+line)`，
      需要确认 76 对应的 channel/line（`CXD4115_IRQ_OFFSET 32` 暗示有偏移，不能直接 76→4*16+12）。
- [ ] **status 需要哪些位**：DMP 库可能按位判断"哪条管线完成"。先给 `0x1` 试。
- [ ] 驱动 `sugilite_ioctl`（.ko 内 `0x8E4`）还有一组命令号，可能用于版本/时钟/挂起，
      与完成信号无关，暂不建模。

---

## 6. 一句话总结

> 缺的不是"逆向"，是一块 **16 KB 的假硬件**：`0xf1000000` 里维护一个 `+0x34` 状态字，
> 用户态写完命令就置位并抬 IRQ 76。做完这一步，DMP 初始化才有可能返回，
> 后面才谈得上"App 开始画"。

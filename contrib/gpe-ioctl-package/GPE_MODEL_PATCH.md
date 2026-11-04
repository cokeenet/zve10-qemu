# 给上游（cokeenet/zve10-qemu）的贡献包：GPE 硬件模型 + grm_gles.ko 中断面

> 我们是并行团队，在 QEMU（同款 ma1co/qemu 基底，`-M cxd90045`）里跑 **ZV-1F 固件**
> （供体侧，CXD90045_k8），目标是给"ZV-1F 新菜单 → ZV-E10 移植"造一个无相机的验证台。
> 本文只含研究结论 / 补丁 / 地址表，不含任何 Sony 二进制。License 兼容 PolyForm NC。

## 0. 一句话

**你们用 fake_gles 替掉 grm_gles；我们走"真 grm_gles.ko + QEMU 硬件模型"，**
**已验证 `DMP_2D_Initialize` 返回 0（8 次 kick / 8 次 IRQ / 8 次 ack，严格 1:1）。**
这直接解掉你们当前卡点的一半：`grm_ma::mspace` 全 `0x5A5A5A5A` 的根因是
fake_gles 不提供内核接口（dmm ──uses──> grm_gles），换回真驱动即可。

## 1. 你们仓库缺失的一块：中断面（全仓库 0 处提及 IRQ 76）

`grm_gles.ko`（未 strip，vaddr==.text 偏移）的硬件面完整表：

### 1.1 中断注册

```c
request_irq(76, sugilite_interrupt, IRQF_DISABLED|IRQF_TRIGGER_HIGH, "dmpgles2", dev)
/* 0x84 = 电平高有效 ⇒ 模型必须靠 ack 自清，否则中断风暴 */
```

### 1.2 `sugilite_interrupt`（.ko 0x640）逐条语义

```c
st = ioread32(bar + 0x34);
if (st == 0) BUG();                                /* ★ 空中断 = 内核 BUG，先置状态再抬线 */
if (st & 0x80000000) iowrite32(0, bar + 0x1000);
if (st & 0x40000000) iowrite32(0, bar + 0xC18);
if (st & 0x04000000) iowrite32(0, bar + 0x1C);     /* ← bit26 是 2D 完成位 */
if (st & 0x0000000F) { iowrite32(1, bar+0x3C); iowrite32(1, bar+0x44); }
dev->[0xa8] |= (st & 0xFFFF0000) | (ioread32(bar+0x34) & 0xFFFF0000);
__wake_up(dev + 0x4C, 1, 1, NULL);                 /* 只 wake_up，从不 complete_all */
```

要点：
- `dev->a8` 只累加**高 16 位**；ISR ack 前后各读一次 0x34，第二次读到 0 不影响判定
  （a8 用第一次的 st），⇒ 模型在 ack 时立即清状态是安全的（无竞态）。
- `0xC0048203` 的完成判定 = `tst(arg & 0xC4000000, dev->a8)`，命中后 `bic` 清位。
  **libObj 的等待掩码恒为 bit26**（写死在 libObj `0x12cc2c: mov.w r2,#0x4000000`）。

### 1.3 用户态门铃（2D 命令不走 ioctl，纯 /dev/mem MMIO）

`rm_reg_init`（libObj 导出符号 @0x12c3c0）：open dmpgles2 → ioctl 取 BAR →
`/dev/mem` mmap 16KB @0xF1000000 → 直接写寄存器。提交序列（libObj `0x12cb60`）：

```
BAR+0x1804 = desc_phys    ← 先写：命令描述符的 DDR0 物理地址（实测 0x2056e270）
BAR+0x1800 = count        ← 最后写 = kick（实测值 4）
```

⚠ `0x12c990` 有门控：`g_flag==0` 时 0x1800 不写。`BAR+0xC0` 不是门铃
（bypass 配置位，值域 0x20000000/0x20000001）。

### 1.4 ioctl 语义修正表（与 zv-e10-re 的 17 条对照后的关键几条）

| cmd | 语义 | 阻塞性 |
|---|---|---|
| 0xC0048202 | 取 idx→phys 表（BAR 地址来源） | 不阻塞 |
| **0xC0048203** | 等 GPE 完成位（waitqueue dev+0x4c，超时 20*table[idx] jiffies） | ★ 唯一 IRQ 依赖 |
| 0xC004820D | 等事件 completion（超时 10^7 jiffies）；等之前 `node->[0x14]=0` 清掉 open 时的 complete_all ⇒ **第一次就真等** | 只能被 0xC004820E POST |
| **0xC0048211/12** | `down/up(&dev->sem[idx])`，二值信号量（dev+0xb0+idx*0x14），**纯内核对象，ISR 不参与** | 0x11 无超时 |

## 2. 可直接合入的 bionz.c 模型补丁（~60 行，最终验证版）

```c
#define GPE_BASE 0xf1000000u
#define GPE_SIZE 0x4000u

static struct { uint32_t regs[GPE_SIZE/4]; uint32_t int_status; qemu_irq irq; } gpe;

static uint64_t gpe_read(void *o, hwaddr off, unsigned sz)
{ return off == 0x34 ? gpe.int_status : gpe.regs[off >> 2]; }

static void gpe_write(void *o, hwaddr off, uint64_t v, unsigned sz)
{
    if (off == 0x1800) {                       /* 门铃：唯一 kick 点（0x1804 只存不抬） */
        gpe.regs[off >> 2] = (uint32_t)v;
        gpe.int_status |= 0x04000000u;         /* ★ 先置状态 */
        qemu_set_irq(gpe.irq, 1);              /* ★ 再抬线（顺序不能反，否则 ISR BUG） */
    } else if ((off == 0x1C || off == 0xC18) && (uint32_t)v == 0u) {
        gpe.int_status = 0; qemu_set_irq(gpe.irq, 0);     /* ISR ack（写 0） */
    } else if ((off == 0x3C || off == 0x44) && (uint32_t)v == 1u) {
        gpe.int_status = 0; qemu_set_irq(gpe.irq, 0);     /* 低 4 位 ack（写 1） */
    } else {
        gpe.regs[off >> 2] = (uint32_t)v;
    }
}
/* ops: min/max access 1..4, NATIVE_ENDIAN; 建议每个写都 qemu_log_mask(LOG_GUEST_ERROR) 取证 */
/* 挂载：cxd90045_init() 里 irq[] 填充循环【之后】：
     gpe.irq = irq[76 - 32];   ← = irq[44]，别写成 irq[76]（数组 224 项） */
```

安全性（都核实过）：
- `sugilite_register_init` 的 34 个写偏移里**没有 0x1800/0x1804** ⇒ 开机 0 次误抬线；
- `BAR+0x1C` 双用途（init 写 0x10000 是配置、ISR 写 0 才是 ack）⇒ 按**写入值**判定；
- 一条 raise 恰好一条 ack，电平自清无风暴。

验证日志（我们侧实测）：GPEW 42 条（0x1800×8 / 0x1804×8）、raise 8、ack 8+1（init 期 0xC18=0），
`app.err` 出现 `===== DMP_2D_Initialize : 0`。描述符内容已 dump（8 字一组重复结构，
含 DDR 指针），与你们 `GRM_REQUEST_QUEUE.md` 的方向一致。

## 3. 对你们两条线的影响

- **QEMU 线**：换回真 grm_gles.ko + 上面的模型 ⇒ dmm 恢复依赖、MA mspace 不再是垃圾；
  不需要懂 GPE 命令语义就能让提交-完成闭环（收到 kick → bit26 → IRQ 76）。
  你们 fake_gles 的 320x240→VIP→screendump 通路可继续做兜底渲染。
- **真机线（nflasha15_hybrid_v4）**：不受影响，实体机不需要任何模型 —— 但注意
  你们的 `libIMDB` v5 补丁（10 处 mask）我们复算过影响面，与你们结论一致：仅 QEMU。

## 4. 我们侧的现状（供参考 / 交换）

- ZV-1F 固件在 QEMU：内核→init→App 50 线程稳定运行；卡点推进到
  `infra_indctr_exit`（libObj+0x20594c `ldr r1,[r3,#0]`，this==NULL），与你们
  round 78 的 libMWF pc=0 形态相似（未注册回调/空对象），互相有参照价值。
- 独立完成 L1 ABI 闭合复核（appFw 717 未定义符号，借 6 库后余 3，与移植侧口径一致）。
- **希望交换的**（都只读、小体量）：
  1. nflasha user area 头 64 KB 的 Sony 私有分区表（我们的 eMMC 整盘镜像卡在
     `nflasha: unknown partition table`，需要 magic/`partinf.conf` 对照——你们有整盘 dump）；
  2. 真机 `/setting/psd.bin`、`appfw_infra/`、`mode/` 的文件清单（App 初始化分支可能依赖）；
  3. 若方便：`alog.bin` 384 KB 的开头若干段（我们做 ZV-1F 侧对照）。

## 5. 附：ZV-1F 作为 QEMU guest 的三个坑（同平台经验，可能有复用价值）

1. Sony `setup_arch` 的 kemco 路径要求固定 cmdline 放物理 `0x103000`；
2. 内核镜像偏移 0x28 处 `mvn r2,#0` 破坏 ATAGS 指针，需补 `mov r2,#0x100`；
3. 两个 SD 卡挂同一条 sd-bus 会让 eMMC 探测死在 CMD1（`Unexpected IRQ Status 0x18000`）——
   只留一张卡 + `drive_get(IF_MTD,0,1)` 指整盘镜像。

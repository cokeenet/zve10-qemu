# 可按需提供的证据与额外材料

以下内容**默认不随分支发布**。只在上游需要时逐项提供，且先确认符合对方的分发政策
（不含 Sony 固件 / 二进制 / 副本）。

---

## 我们自己的探针输出（纯文本，可给）

| 本地文件 | 大小 | 内容 |
|---|---:|---|
| `work/KLOG_ioctl.txt` / `KLOG_ioctl2.txt` | 各 18 KB | 早期 ioctl 探针日志 |
| `work/KLOG_ioctl3.txt` / `KLOG_ioctl4.txt` | 各 174 KB | GPEW / GPEIRQ 完整 trace |
| `work/KLOG_ioctl7.txt` | 644 KB | 扩展 trace |
| `work/qemu_gpe_err.txt` | 2.3 KB | `DMP_2D_Initialize : 0` 证据 |
| `work/ko_gles_ioctl_r.txt` | 30 KB | 驱动 ioctl 分支带重定位反汇编 |

42 次 GPEW 写、8 次抬线 / 8 次 ack、kick/IRQ/ack 1:1 都出自这些日志。
`KLOG_ioctl5.bin` / `KLOG_ioctl6.bin`（各 1 MB）是物理内存快照，**暂不提供**，
可能含固件派生字节。

## issue 里我们承诺过的四项

| 项目 | 本地文件 | 处理 |
|---|---|---|
| GPE 模型补丁完整 diff | `GPE_MODEL_PATCH.md` §2（内联 C 代码） | ✅ 已交付 |
| GPEW / GPEIRQ 日志原文 | `KLOG_ioctl3/4/7.txt` | 按需提供，纯文本 |
| 2D 命令描述符原始 dump 字节 | `work/gpe.bin`（16 KB） | ⚠️ 先核来源（疑似 guest 内存抓取）；建议只给重复 8 字结构的十六进制摘录，不给二进制文件 |
| 全 17 条 ioctl 注解 | `GRM_GLES_IOCTL_TABLE.md` | ✅ 已交付 |

## 绝不对处发

- `work/grm_gles.ko` —— 原厂 Sony 内核模块（16 KB）
- `work/fb_gpedst.bin`（1 MB）及其派生 PNG —— 帧缓冲抓取
- `dump_20261028/ptbl.bin`、`Backup.bin`、`nflasha15.img`、`zve10_dump_20261028.zip`
- 任何来自供体固件的 `.elf` / `.so` / `.ko` / `.img` 或分区镜像

## 另外可以贡献的原创分析（对方没要）

这些是**我们自己写的结论**，不是固件字节，无分发问题：

1. **Sony 私有分区表格式** —— `+0x00` magic `82461.00`，`+0x08` `u32 n = 30`，
   `+0x0c` `u32 ver = 1`，`+0x10` 起 30 条
   `{ u16 unk; u16 start_64k; u16 unk; u16 len_64k; u32 type; u32 valid; }`。
   已与 `/proc/partitions` 逐条核对，对 fastboot / eMMC 分区写入路径可能有用。
2. **`osal_uipc.ko` cmd 0–16 语义**与 11389 次 IPC 统计 —— 已在 PR #3 内。
3. **ZV-1F 作 QEMU guest 的三个坑** —— `GPE_MODEL_PATCH.md` §5。
4. **真机只读采集的约束** —— `contrib/REAL_CAMERA_FINDINGS.md`；
   注意其中 LZP / `/usr` 的旧说法已被上游 issue #4 的澄清取代。

## 建议顺序

1. 把本包 push 到分支，贴 A 版中文回复。
2. 等对方模型实测结果；若仍卡住，再给 `KLOG_ioctl3/4.txt` 与 2D 描述符十六进制摘录。
3. 分区表格式作为独立话题，最后再提，别和这次交付混在一起。

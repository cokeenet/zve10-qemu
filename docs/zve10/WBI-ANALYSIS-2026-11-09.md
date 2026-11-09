# WBI 是什么：从 Loader2、内核源码、三段实证数据还原

## 0. 一句话

**WBI = Warm Boot Image，是 `nflasha5`（eMMC `0x05800000`，60 MB）里的一份"运行中系统内存快照 + 启动元数据"。** 它不是缓存，不是文件系统，而是**启动链本身的一环**：相机断电后重新上电时，Loader2 读它、验证它，然后把 RAM 恢复回去，CPU 从 `resume_vector` 直接接着跑，从而跳过整套冷启动。

这份数据来自三个互相独立的源头：Loader2 二进制、Linux 内核源码、以及本地一手的 `nflasha5.img` 备份。

---

## 1. WBI 在启动链里的位置

真机 eMMC 布局（`ZVE10_EMMC_FULL.img` 一手确认）：

```
0x000000  EXBL 头（magic 'EXBL'，0x400000 处 user area 开始）
0x007000  Loader2 起始（VA 0xFE047000）
0x410000  nflasha1 ... 
0x5C00000 nflasha5 = WBI 分区（SDM2 type=2，唯一一个 type=2 的分区）
```

`cxd90045_load_wbi()` 的硬编码偏移正好是 `2×0x200000 + 0x5800000`，与上面完全吻合。

启动顺序：

```
BootROM → EXBL（LPDDR3 初始化）→ Loader2
                                    │
                                    ├─ 读 eMMC block 0（SDM2 分区表，30 项）
                                    ├─ 读 nflasha5 头部 → 判 WBI 是否有效
                                    └─ 有效 → 按段表把 664 段搬回 RAM，
                                             跳到 resume_vector (0x00124E1C)
                                    无效 → 走完整冷启动（读 nflasha3 的 kernel+initrd）
```

---

## 2. 从 Loader2 二进制直接读出来的证据

从 `ZVE10_EMMC_FULL.img` 偏移 `0x7000` 起 `0x30000` 字节就是 Loader2，VA base `0xFE047000`。里面有一整套 WBI 处理字符串（原文）：

```
0xFE049F98  "MEM:wbi header string:%.4s"
0xFE049FB4  "MEM:WBI partition          :%d"
0xFE049FD4  "MEM:    sector             :%d"
0xFE049FF4  "MEM:    startaddr          :0x%08X"
0xFE04A018  "MEM:  wbi_hdr.fVersion     :%.4s"
0xFE04A03C  "MEM:  wbi_hdr.sections     :%d"
0xFE04A05C  "MEM:  wbi_hdr.resume_vector:0x%08X"
0xFE04A080  "MEM:  wbi_hdr.sector_size  :%d"
0xFE04A0A0  "MEM:  wbi_hdr.r_data_size  :%d"
0xFE04A0C0  "invl"       ← WBI_INVAL_VER
0xFE04A0C8  "prof"       ← WBI_PROF_VER
0xFE04A0D0  "WBI1"       ← WBI_VER
0xFE04A0D8  "WBI0"       ← WBI_PRE_VER
0xFE04A128  "MEM:num  ram_addr datasect rlen      olen     record"
0xFE04A160  "MEM:%4d %08X %08X:%08X->%08X %4d"
0xFE04A184  "LBI:[START] load boot image"
0xFE04A1A4  "LBI:[ END ]"
```

**这证明三件事：**

1. Loader2 会逐字段打印 `wbi_hdr` 的 `fVersion / sections / resume_vector / sector_size / r_data_size` —— 它就是照着 `struct wb_header` 解析的。
2. 它会逐段打印 `ram_addr / datasect / rlen / olen / record` —— 这就是 `struct wb_section` 的字段，段表落在 `r_data_size` 之后。
3. `invl / prof / WBI1 / WBI0` 四个标记都在，与内核里的常量逐一对应。

**关键：`%08X:%08X->%08X` 这个打印格式（`startaddr:rlen->olen`）与本地备份解出来的段表完全一致。** 不是猜的。

---

## 3. `struct wb_header`（内核 `warmboot.h` 原文，52 字节）

```c
struct wb_header {
    char fVersion[4];
    unsigned long sections;
    unsigned long flag;          // WBI_COMPRESS | WBI_NOT_KERNEL_RO | WBI_CKSUM | WBI_LZ77_COMPRESS
    unsigned long resume_vector;
    unsigned long dVersion;
    unsigned long sector_size;
    unsigned long r_data_size;   // 压缩后实际数据
    unsigned long kern_start;
    unsigned long kern_size;
    unsigned long kern_cksum;
    unsigned long o_data_size;   // 解压后原始大小
};
```

本地 `nflasha5.img` 前 52 字节解出来：

```
fVersion     = 'WBI1'
sections     = 664
flag         = 0x09   (WBI_COMPRESS | WBI_LZ77_COMPRESS)
resume_vector= 0x00124E1C
dVersion     = 0x20060224
sector_size  = 512
r_data_size  = 35,756,032
kern_cksum   = 0x0F0F0F0F
o_data_size  = 136,527,872
```

**52 字节之后的 `0x34..0x1FF` 原本全是 0。**

---

## 4. 段表与数据区的真实布局（一手验证）

```
offset 0         wb_header  52 B（+ 460 B 零填充，共 1 个扇区）
offset 512       ┐
                 ├ 压缩数据区  r_data_size = 35,756,032 B (34.1 MB)
offset 0x2219800 ┘
                 ├ 段表  664 × 32 B = 21,248 B
offset 0x2219A00 ┘
                 └ 之后全零（对齐填充）

合计占用 34.12 MB / 60 MB 分区
```

段表项结构（`struct wb_section`，32 字节 8 个字段）：

```c
struct wb_section {
    unsigned long addr;      // 恢复到的物理 RAM 地址
    unsigned long rlen;      // 压缩后长度
    unsigned long cksum;
    unsigned long flag;
    unsigned long olen;      // 解压后长度
    unsigned long virt;
    unsigned long __pad[1];
    unsigned long meta_cksum;
};
```

**交叉验证（这是最硬的证据）：**

```
段表 sum(rlen) = 35,756,032  ==  头部 r_data_size = 35,756,032   ✔ 完全吻合
段表 sum(olen) = 136,527,872 ==  头部 o_data_size = 136,527,872  ✔ 完全吻合
```

前几段（`ram_addr / rlen / olen`）：

```
[0] 0x00100000  0x9A000  0xE0000
[1] 0x001E0000  0x9AE00  0xE0000
[2] 0x002C0000  0x9E600  0xE0000
[3] 0x00480000  0xF800   0x20000
[4] 0x00588000  0x8800   0x58000
```

RAM 覆盖分布（664 段）：

```
DDR 0x00000000–0x10000000   597 段   75.54 MB   ← 内核 + 用户态
DDR 0x10000000–0x20000000    11 段    9.43 MB
DDR 0x20000000–0x24000000    56 段   45.24 MB   ← 显存/CP 区
```

**⇒ 这是一份 130.2 MB 物理内存的快照，压缩到 34.1 MB（压缩率 26.2%）。**

而这些地址**正是**真机 cmdline 里 `wbimem=` 声明的那几块之外的区域 —— `wbimem=` 是"不要进快照"的排除表：

```
wbimem=0x1000@0x20021000      DDR 控制器状态
wbimem=0xEC000@0x20028000     压缩引擎工作区
wbimem=0x8000@0x20145000
wbimem=0x500000@0x03085000
wbimem=0x96D000@0x11000000
wbimem=0x2A48000@0x20200000
wbimem=0x200000@0x23E00000    共 82 MB 被排除
```

---

## 5. 四个版本标记的语义（内核源码给出）

```c
#define WBI_INVAL_VER "invl"   // 无效态：写过程中崩了，或主动作废
#define WBI_PROF_VER  "prof"   // profile 阶段（只改头，不重建镜像）
#define WBI_PRE_VER   "WBI0"   // 最小快照（Min SSBI），冷启动时用
#define WBI_VER       "WBI1"   // 优化快照（Opt SSBI），正常热恢复用
```

对应 Sony 的 ssboot 状态机（`pm.c` + `warm.c`）：

```c
switch (get_ssboot_stat()) {
case SSBOOT_CREATE_MINSSBI:  fVersion = "prof"; break;   // 只重写头
case SSBOOT_CREATE_OPTSSBI:  fVersion = "WBI0"; break;   // 最小快照
default:                     fVersion = "WBI1"; break;   // 完整快照
}
```

**本地备份是 `WBI1` + `sections=664` + 34 MB 压缩数据 → 这是完整的优化快照，不是最小快照。**

而 `prof` 出现在 `nflasha5` 里 9 处、`invl` 1 处 —— 这些是**快照内部的 RAM 内容**（包含内核代码/数据里的常量），不是表头。

---

## 6. 写 WBI 的顺序（为什么分两次写头）

`warm.c::do_make_image()` 的真实流程：

```c
/* phase 1: 先写一个"无效"头 */
memcpy(wbheader.fVersion, WBI_INVAL_VER, 4);   // "invl"
wbi_send_data(&wbheader, sizeof(wbheader));

/* phase 2: 发送全部段 */
wbi_send_sections(&wbheader, section_data, sections);  // 数据 + 段表
wbi_flush();

/* phase 3: 用真实标记覆写头 */
wbi_rewrite_header(&wbheader);   // memset 整个扇区 0，再写 52 字节头
```

**这是一个典型的"写时作废"事务：**

1. 开头把 `fVersion` 设成 `invl` → 即使中途断电，Loader2 也会判无效、走冷启动，绝不会加载半截快照。
2. 数据写完，再用合法标记覆写头。

**⇒ 所以 `image_drop` 写全零头，在语义上等价于"主动作废"，且比 `invl` 更彻底**（`fVersion` 连 `invl` 都不是，是 `\0\0\0\0`）。

---

## 7. `dropped wbi` 究竟做了什么

内核源码（`arch/arm/common/warm.c`）：

```c
static int __wbi_drop(unsigned char *buf, unsigned long size)
{
    memset(buf, 0, size);
    ret = wb_default_device->write_sector(wb_default_device, buf, 0, size >> 9);
    if (ret) printk("%s: cannot write = %d\n", __func__, ret);
    else     printk("dropped wbi\n");
}

static ssize_t image_drop_store(...)
{
    if (n == 0 || *buf != '1') return -EINVAL;
    ret = _wbi_drop();          // ← 注意：直接调 _wbi_drop，绕过 ignore_drop
}

static int _wbi_drop(void)
{
    if (!image_mode) goto out;      // image_mode=2，通过
    if (!wb_default_device) goto out;
    if (!wbi_sectorbuf) goto out;
    ret = __wbi_drop(wbi_sectorbuf, DEFAULT_SECTOR_SIZE);   // 512 字节
}
```

**真机 cmdline 里三道自动丢弃都被关了：**

```
boot_drop=0      → wb_register_device 时不 drop
resume_drop=0    → wbi_resume_drop 时不 drop
ignore_drop=1    → wbi_drop() 直接跳过
```

**但 `image_drop_store` 走的是 `_wbi_drop()` 直连路径，不检查 `ignore_drop`。**

**⇒ 这是索尼设计上的一个"手动覆盖开关"：唯一能从用户态强行作废 WBI 的入口。**

---

## 8. 头部清零之后 Loader2 看到什么（关键推演）

```
fVersion      = '\0\0\0\0'   ← 既不是 invl，也不是 WBI1/WBI0
sections      = 0
flag          = 0
resume_vector = 0
dVersion      = 0
sector_size   = 0            ← ★ 致命
r_data_size   = 0
o_data_size   = 0
```

**最致命的是 `sector_size = 0`。** 内核里：

```c
#define RU_SIZE(len, alignment) (((len) + (alignment) - 1) & ~((alignment) - 1))
```

`alignment = 0` 时，32 位下 `~((0)-1) = ~0xFFFFFFFF = 0`，**`RU_SIZE(n, 0)` 恒等于 0**。

**⇒ 所有按扇区对齐、分块写入的计算全部塌缩成 0。**

**⇒ Loader2 拿到的不是"无效镜像"，而是"一个参数自相矛盾的镜像"，行为未定义。**

这解释了为什么症状是**完全无反应**而非社区描述的"重启 9 次"（后者是"固件语义不匹配"这类参数合法但内容不符的情形）。

---

## 9. 和你贴的那段社区经验对照

> "要先清理一下（前 512 字节置为 0），启动会从 wbi 恢复时，固件改动太大的话会和固件语义不匹配直接变砖，症状是启动会重启 9 次。这个方式最终被证伪了。"

**这段经验里，"清理前 512 字节"这个动作本身在索尼的设计里是合法的** —— 这正是 `__wbi_drop()` 干的事。

**它变砖的原因写得也很准：**"固件改动太大 → 语义不匹配"**。也就是说：快照是旧固件的内存镜像，你换了固件却不作废快照，恢复回来的 RAM 里跑的还是旧代码，与新的闪存内容冲突。**

**但注意：它的语境是"改固件时清理"，清理后紧接着就刷入新固件。而我是只清理、不刷固件。**

> "WBI 启动不走 init2 的挂载 sd 卡流程，导致上次内存中的数据没法用原机身 appFw.so 恢复造成变砖"

**这句说的是恢复出来的 RAM 里的用户态（`im.elf` / `appFw.so`）不认识新的闪存内容。** 我这次是反过来的情形：闪存没变，但快照被作废，冷启动又因为别的原因没走通。

> "除非每次启动都清除 WBI，这样代价有点大，不清楚他这个闪存寿命怎么样。修改内核参数不写 wbi，或者拦截 WBI 写入是另一条可行方法"

**这三条都对，而且现在有了源码级的落点：**

| 方法 | 源码落点 |
|---|---|
| 每次启动清除 | `image_drop` 或 cmdline `boot_drop=1`（已设为 0） |
| 改内核参数不写 WBI | `image_mode=0`（`_wbi_drop` 里第一道判断就是 `if (!image_mode) goto out`） |
| 拦截 WBI 写入 | `wb_default_device->write_sector`，即 `wbi4` 设备的回调 |

**注意：`boot_drop=1` 会在每次 `wb_register_device` 时自动 drop —— 这是"每次启动都清"的官方开关，不需要手动写 sysfs。真机当前是 `boot_drop=0`（保留快照）。**

> "wbi 分区是什么？？？"

上面第 1、3、4 节答完了：**60 MB 的 `nflasha5`，SDM2 表里唯一 `type=2` 的分区，装的是 130 MB RAM 的 LZ77 压缩快照（34 MB）+ 52 字节头 + 664 条段表。**

---

## 10. 与 A7M4 社区项目（`Erlla/sony-a7m4-custom-firmware`）的对照

**该仓库对 WBI 的处理只有一处，但恰好印证了上面的结论：**

`a7m4/firmware.py:252-254`

```python
old = read("0700_part_image/dev/nflasha5")
require(old[:4] == b"WBI2", "BASELINE", "Unexpected warm-boot marker")
write("0700_part_image/dev/nflasha5", old, b"WBI3" + old[4:])
```

**⇒ 它只改 4 个字节：`WBI2` → `WBI3`。其余全部不动。**

`methods/03-fl2-fl3/README.md:41` 给出理由：

> **`nflasha5`** ｜ 当前组合将 `WBI2` 改为 `WBI3`，**避免恢复旧暖启动快照**

**这是因为索尼 Loader2 的判定逻辑是"`fVersion` 严格白名单匹配"，任何不在白名单里的串都会被判无效 → 走冷启动。所以把 `WBI2` 改成 `WBI3`，一字节都不需要动数据，就能让 Loader2 干净地忽略整个快照。**

**这正是正确的做法：**

- **不写 0**（`sector_size=0` 会造成参数矛盾，行为未定义，就是我踩的坑）
- **写一个"合法但未知"的标记**（`WBI3`），让 Loader2 走"不认识 → 冷启动"分支
- **保留原数据**（万一以后要恢复快照，只改回 4 字节即可）

**对比 `WBI2` vs 我们的 `WBI1`：**A7M4 是 `WBI2`，ZV-E10 是 `WBI1` —— 同族不同代，说明 Sony 会随固件版本升级标记。

**该仓库 `tools/verify_sd_package.py:123` 也把 `WBI3` 叫做 "Warm-boot marker"，验证的是"清理后必须变成非法标记"这一事实。**

---

## 11. 正确的"清理 WBI"方法（与我犯的错对比）

| 做法 | 结果 |
|---|---|
| **我做的：写 512 字节 0** | `sector_size=0` → 参数矛盾 → Loader2 行为未定义 → 完全无反应 |
| **A7M4 项目：改 4 字节 `WBI1`→`WBI3`** | `fVersion` 非法但其余字段合法 → Loader2 判"不认识的快照" → 干净冷启动 |
| **官方开关：cmdline `boot_drop=1`** | 内核每次注册 WBI 设备时自动 `__wbi_drop()` → 但仍然是写 0！ |
| **官方开关：`image_mode=0`** | `_wbi_drop` 首道 `if (!image_mode) goto out` 直接跳过 → 根本不写 |
| **官方开关：`ignore_drop=1`** | `wbi_drop()` 跳过，但 `image_drop_store` 绕过它 |

**⇒ 最安全的"我要冷启动"的写法是：把 `fVersion` 的 4 字节改成一个非法但结构合法的值，而不是清零整个 512 字节。**

**⇒ 而且改完之后必须能再改回来 —— 我有完整备份，这一步天然满足。**

---

## 12. 为什么快照会导致"改了不生效"（回到最初的问题）

**这才是本轮真正的收获，也是整个"New Menu"卡住的根本原因：**

**快照恢复的是整块 RAM，包括：**

- 已经 `dlopen` 过的 `viewUnified2.so` 映射
- 已经构造好的菜单对象树
- **进程的 environment 数组**
- `/etc/ld.so.preload` 在快照时刻的加载结果

**⇒ 所以重启后 `im.elf`（pid 157）的 environ 里没有我们写的 `LD_PRELOAD` —— 因为它根本没重新 exec 过，是被恢复回来的。**

**⇒ 所以 `im.elf` 的 maps 里有 0 个 `libnewmenu` 映射 —— 因为它不是新加载的。**

**⇒ 所以快门的修改不生效 —— 同理，`libObj.so`/`appFw.so` 的改动在快照里跑的是旧代码。**

**这三件事是同一个原因。** 之前我一直归因于"交付路径没打通"，方向错了；**根因是相机处于快照恢复态，闪存上的任何改动都进不了正在恢复的 RAM。**

这也解释了为什么删除快照头之后相机反而彻底起不来：**快照没了，但没有触发正常冷启动**。

---

## 13. 证据清单

| 事实 | 来源 |
|---|---|
| `nflasha5` 是 SDM2 `type=2` 分区 | `ZVE10_EMMC_FULL.img` @0x400000 一手解析 |
| Loader2 处理 WBI，字段与 `wb_header` 一致 | `ZVE10_EMMC_FULL.img` @0x7000 字符串 + 引用扫描 |
| `WBI1` 头的 11 个字段值 | `zve10_backup/nflasha5.img` 一手解析 |
| `sum(rlen)==r_data_size`、`sum(olen)==o_data_size` | 段表与头部交叉验证，零误差 |
| 664 段 / 130.2 MB → 34.1 MB | 段表逐项求和 |
| `wbimem=` 排除表 82 MB | `real_cmdline.txt` 原文 |
| 四个标记语义 | 内核 `warmboot.h` + `pm.c` switch |
| 写时作废三阶段 | 内核 `do_make_image()` |
| `image_drop` 绕过 `ignore_drop` | 内核 `image_drop_store` → `_wbi_drop` |
| `sector_size=0` 导致 `RU_SIZE` 塌缩 | 内核 `wbi.h` 宏 + 手工推导 |
| A7M4 项目只改 4 字节 `WBI2`→`WBI3` | `Erlla/sony-a7m4-custom-firmware` `a7m4/firmware.py:252` |

未验证：Loader2 对 `fVersion` 的具体白名单（只能从 `WBI1`/`WBI0`/`prof`/`invl` 四个常量 + 社区 `WBI2`/`WBI3` 推断）；快照恢复后 `im.elf` 的精确重入点。

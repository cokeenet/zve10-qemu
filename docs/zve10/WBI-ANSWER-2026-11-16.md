# 为什么清 WBI 起不来？为什么 QEMU 的 WBI 是另一回事？

## 一句话回答

```text
QEMU 里我"搞的 WBI"根本不是清 WBI，而是**把 nflasha5 的 7 段数据搬进内存**。
清 WBI 是反过来的操作 —— 把 WBI 的头 512 字节清零。
两者方向相反，不是同一件事。
```

---

## 一、QEMU 里的「WBI」到底是什么（源码级证据）

之前在 QEMU 机模 `hw/arm/bionz.c` 里补的那一步：

```c
static const struct { uint32_t size, addr; } cxd90045_wbi_segs[] = {
    { 0x001000, 0x20021000 }, { 0x0EC000, 0x20028000 },
    { 0x008000, 0x20145000 }, { 0x500000, 0x03085000 },
    { 0x96D000, 0x11000000 }, { 0x2A48000, 0x20200000 },
    { 0x200000, 0x23E00000 },
};

static void cxd90045_load_wbi(BlockBackend *blk)
{
    hwaddr src = 2 * 0x200000 + 0x05800000;   /* 用户区里的 nflasha5 */
    ...
    for (i = 0; i < ARRAY_SIZE(...); i++) {
        /* 从 nflasha5 顺序读出，写到对应物理地址 */
        rom_add_blob_fixed("wbi", buf, chunk, dst + done);
    }
}
```

**⇒ 这一步是**读 nflasha5、搬进内存**。
   注释里写着：合计 ≈ 59.7 MB ≈ nflasha5 的 60 MB，即 **nflasha5 就是这 7 段的顺序拼接**。

**⇒ 真机由**引导器（Loader2）**做这件事。
   QEMU 用 `-kernel` 直载内核会跳过引导器 ⇒ 所以我要手工补上"搬段"这一步。

### 关键区分

| 操作 | 方向 | 谁做 | 效果 |
|---|---|---|---|
| **装载 WBI** | nflasha5 → 内存 | 引导器（真机）/ QEMU 机模 | 冷启动时把上次快照塞进 RAM |
| **清 WBI / drop** | 把 nflasha5 头 512B 清零 | 内核 `__wbi_drop()` | 让引导器认为"没有有效快照" |

**QEMU 那次是第 1 行。你现在想的是第 2 行。完全相反。**

---

## 二、清 WBI 为什么起不来（真正的原因，不是"清了所以不启动"）

### 2.1 清空本身是成功的

内核 `__wbi_drop()`（`warm.c:365`）：

```c
static int __wbi_drop(unsigned char *buf, unsigned long size)
{
    ...
    memset(buf, 0, size);                                    /* 512 字节清零 */
    ret = wb_default_device->write_sector(wb_default_device, buf, 0, size >> 9);
    if (!ret) printk("dropped wbi\n");
    ...
}
```

**⇒ 清空 = 把 nflasha5 的**头 512 字节**写成全零。**这一段确实成功了。**

### 2.2 引导器（Loader2）如何判断

四个 predicate（`Loader2 @ 0xFE047000` 反汇编）：

```text
is_invl  0xFE049CCE   memcmp(hdr[0..3], "invl", 4) == 0
is_prof  0xFE049CF0   memcmp(hdr[0..3], "prof", 4) == 0
is_WBI1  0xFE049D10   memcmp(hdr[0..3], "WBI1", 4) == 0
is_WBI0  0xFE049D30   memcmp(hdr[0..3], "WBI0", 4) == 0
```

**全零头的 `fVersion` 是 `\0\0\0\0` ⇒ 四次 memcmp 全部非零 ⇒ 四个 predicate 全返回 0。**

**⇒ 它们不是"检测到无效就报错"，而是"检测到特定版本才返回 1"。**
**⇒ 全零头落到的不是错误分支，是"什么都不是"。**

### 2.3 `cldboot` 这条路也返回 0

启动模式表（`0xFE055560`，26 项 × 32 字节）：

```text
[22] ssboot   -> 0xFE049A27  (is_WBI1)
[23] ss_bis   -> 0xFE049A35  (is_WBI0)
[24] cldboot  -> 0xFE049A43   ← 0xFE049A42 是  movs r0,#0 ; bx lr
[25] error    -> 0xFE049A43
```

**`cldboot` 的 handler 是恒返回 0 的函数 ⇒ 冷启动判定同样依赖 predicate。
⇒ 全零头下 cldboot 也不成立。**

### 2.4 ★ 真正的坑：`warm.c:418` 的 `wbi_ignore_drop`

```c
int wbi_drop(void)
{
    if (!wbi_ignore_drop) {      /* ← 本机 wbi_ignore_drop = 1 */
        ret = _wbi_drop();
    }
    return ret;
}
```

**本机内核参数：`warm.ignore_drop=1`**
（这是 `module_param_named(ignore_drop, wbi_ignore_drop, bool)`，`warm.c:143`）

**⇒ `wbi_drop()` 这条自动路径被**关掉**了。**

**但还有**绕过去**的口子：**

```c
ssize_t image_drop_store(...)     /* warm.c:445 */
{
    if (n == 0 || *buf != '1') return -EINVAL;
    ret = _wbi_drop();            /* ← **直接调 _wbi_drop，不看 ignore_drop** */
    ...
}
```

**⇒ `/sys/power/image_drop` 写 1 是**唯一**能强行清空的口子（Sony `ud_wbi_drop.elf` 就用它）。**

### 2.5 ★★ 顺序陷阱：先 drop 再 mode=0，反过来就白清

```c
static int _wbi_drop(void)          /* warm.c:392 */
{
    down(&wb_device_sem);
    if (!image_mode) goto out;      /* ← ★ 第一道门：mode=0 就直接跳过 */
    ...
    ret = __wbi_drop(...);
out:
    ...
}
```

**⇒ 如果**先**写 `image_mode=0`，再写 drop ⇒ `_wbi_drop()` 第一行就 return ⇒ **什么都没清。**

**⇒ 必须**先 drop（此时 mode 必须非 0）**，**再**写 mode=0。**

**而 mode=0 的作用是阻止关机时重建镜像：**

```c
int cxd_create_warmbootimage(unsigned long entry, unsigned long adjust_vma)   /* warm.c:1215 */
{
    if (!WBI_MODE(pm_state, image_mode))    /* (state==PM_SUSPEND_DISK && mode) */
        return 0;                            /* ← mode=0 ⇒ 直接返回，不重建 */
    ...
    return do_make_image(entry, adjust_vma);
}
```

**⇒ 顺序必须是 `drop(1) → mode(0)`，这是 Sony a7c2-tools 的 `wbi.c` 里
   `inspect or drop1 -> mode0` 那个箭头的含义。**

---

## 三、清 WBI 后为什么"一点反应都没有"

```text
1. Sony 工具把 nflasha5 首 512 字节清零      ← 成功
2. 用 fastboot reboot 重启                    ← ★ 问题在这里
3. 内核启动，image_mode=2，pm_state=PM_SUSPEND_DISK
4. 关机流程走 warm_boot_pm_prepare()
5. WBI 已无效，但代码仍尝试走快照路径
6. 电源转换没有干净完成 ⇒ 相机停在"既没挂起也没重启"的状态
```

**表现就是**：USB 不枚举、不亮、不响。

**⇒ 结论：清 WBI 本身没错，错在**用软件重启**。**

| 做法 | 结果 |
|---|---|
| 清 WBI + `fastboot reboot` | **卡死**（实测两次） |
| 清 WBI + 拔电池 ≥30 秒 | 应当正常冷启动（**还没成功验证过**） |
| 不清 WBI + 软重启 | 从旧快照恢复（配置不生效） |

---

## 四、★ 我们当前遇到的问题和"清 WBI"其实是**同一个根因**

```text
阻碍：拔电池也走温启动 ⇒ bootin 不执行 ⇒ dmode/preload 不生效
证据：im.elf starttime = 0 jiffies < bootin starttime = 97 jiffies

⇒ 因为 WBI 快照**有效**，引导器走了 ssboot/is_WBI1 分支
⇒ 从快照恢复所有进程 ⇒ bootin 没执行

★ 所以：**清 WBI + 拔电池，正是解决这个阻碍的正确做法！**
   - 清掉 → 引导器四个 predicate 全不成立 → 走真正的冷启动
   - 冷启动 → bootin 真执行 → 读 dmode/preload → 库进 im.elf
   - 而且这次不需要每次断网（配置是开机自动生效的）
```

## 五、但必须遵守的硬约束（来自内核源码）

```text
✗ 绝对不能先 mode=0 再 drop（会白清）
✓ 顺序：echo 1 > /sys/power/image_drop  →  echo 0 > /sys/power/image_mode
✓ 清完**必须拔电池 ≥30 秒**，不能用 reboot/poweroff
✗ 不能指望"软件重启"来完成冷启动
```

**而且清之前必须备份：**

```text
已有：
  相机 /log/.wbi_full.bak       60 MB  sha aa584ec9…
  SD 卡 /log/sd/wbi_full.bak    60 MB  sha 相同
  固件包出厂镜像                 34 MB  索尼原厂
```

---

## 六、回答你的两个问题

**Q1：为什么清 WBI 启动不了？**
```text
A: 不是"清了不启动"，而是"清了还用软件重启"。
   内核 image_mode 仍是 2，关机时仍走快照路径，但快照已无效
   ⇒ 电源转换卡在中间状态 ⇒ 不亮不响。
   正确做法是清完拔电池。
```

**Q2：QEMU 的 WBI 怎么搞的？**
```text
A: QEMU 里做的是**装载**：把 nflasha5 的 7 段（59.7 MB）按 wbimem= 段表
   从 eMMC 搬进内存（0x20021000 / 0x20028000 / … / 0x23E00000）。
   那是**替代引导器**读快照给内核用。
   和"清 WBI"方向相反。QEMU 那边从来没做过 drop。
```

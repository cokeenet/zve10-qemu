# 如何正确"清除 WBI"：a7c2-tools 的做法与我犯错的差异

## 0. 先说结论

**`a7c2-tools` 确实会"清 WBI"，用的命令和我完全一样：向 `/sys/power/image_drop` 写 `'1'`。**
**但它紧接着做了一步我没做的关键动作：把 `/sys/power/image_mode` 写成 `'0'`。**

**这两步的先后顺序不可颠倒，原因在内核源码里写着。**

---

## 1. a7c2-tools 的原始实现（`src/fl23/native/wbi.c`）

```c
/* Native vendor sysfs sequence only: inspect or drop1 -> mode0.
 * No raw block writes, firmware/settings writes, shell or power commands. */
```

**逐行流程：**

```c
read_text("/sys/power/image_mode", before_mode, ...);
if (n != 2 || !same(before_mode, "2\n", 2)) return -22;      // ① 前置校验

read_text("/sys/power/image_dev", before_dev, ...);
if (n != 6 || !same(before_dev, "*wbi4\n", 6)) return -22;    // ② 前置校验

if (!req.mode) return;                                        // mode=0 只读检查

op("/dev/kmsg", ...);                                         // ③ 打开 kmsg 准备取证

change("/sys/power/image_drop", '1', &drop_write, &drop_close);  // ④ 作废镜像
if (drop_write == 1) {
    change("/sys/power/image_mode", '0', &mode_write, &mode_close); // ⑤ 关掉重建
}

read_text("/sys/power/image_mode", after_mode, ...);
if (!same(after_mode, "0\n", 2)) r.status = -84;              // ⑥ 回读验证

/* 扫 /dev/kmsg 找 "dropped wbi" 作为新鲜证据 */               // ⑦ 取证
```

**和我做的对比：**

| 步骤 | a7c2-tools | 我 |
|---|---|---|
| 前置校验 mode==2、dev==*wbi4 | ✅ | ✅（读过，但没当门禁） |
| `echo 1 > image_drop` | ✅ | ✅ |
| **`echo 0 > image_mode`** | ✅ | ❌ **漏了** |
| 回读验证 mode==0 | ✅ | ❌ |
| dmesg 取证 `dropped wbi` | ✅ | ✅（看到了） |

**⇒ drop 这一步我们俩做的是**同一个内核函数 `__wbi_drop()`，写同样 512 字节。**

**⇒ 差别只有第 ⑤ 步。**

---

## 2. 为什么 mode=0 必须紧跟其后（内核源码）

### 2.1 没有 mode=0，关机会把镜像重新生成

`pm.c` 的关机路径：

```c
int cxd900x0_create_warmbootimage(void)
{
    switch (cxd900x0_ssboot_stat) {
    case SSBOOT_CREATE_MINSSBI:
        return cxd_create_warmbootimage(cpu_resume_profile, 0);
    case SSBOOT_CREATE_OPTSSBI:
        cxd_create_warmbootimage(cpu_resume_ssboot_pre, 0);
        em_reboot(1);  return 0;
    default:
        return cxd_create_warmbootimage(cpu_resume, 0);   // ← 正常关机走这里
    }
}
```

而 `cxd_create_warmbootimage()`（`warm.c:1215`）：

```c
int cxd_create_warmbootimage(unsigned long entry, unsigned long adjust_vma)
{
    if (!WBI_MODE(pm_state, image_mode))     // ← 门禁
        return 0;
    if (wb_default_device == NULL)
        return 0;
    return do_make_image(entry, adjust_vma); // 重建镜像
}
```

```c
#define WBI_MODE(state, mode) ((state) == PM_SUSPEND_DISK && (mode))
```

**⇒ `image_mode = 0` 时 `WBI_MODE(...) = 0` ⇒ `cxd_create_warmbootimage()` 直接返回 ⇒ **关机不重建镜像**。**

**⇒ 而如果 mode 还是 2，你 drop 之后一关机，内核会**立刻把镜像重新写回去** —— 你的清理白做了。**

### 2.2 为什么必须先 drop、后写 mode=0

`_wbi_drop()`（`warm.c:392`）：

```c
static int _wbi_drop(void)
{
    down(&wb_device_sem);

    if (!image_mode)          // ← ★ 第一道判断
        goto out;             //    mode=0 就直接跳过，什么都不写
    if (!wb_default_device) goto out;
    if (!wbi_sectorbuf)     goto out;

    ret = __wbi_drop(wbi_sectorbuf, DEFAULT_SECTOR_SIZE);
out:
    up(&wb_device_sem);
    return ret;
}
```

**⇒ 如果你**先**写 mode=0，再写 drop，`_wbi_drop()` 在第一行就 `goto out` —— **完全没执行，镜像完好无损，你以为清了其实没清。**

**⇒ 所以顺序是强制性的：先 drop（此时 mode 必须非 0），再 mode=0（阻止关机重建）。**

**a7c2-tools 的代码注释第一行就写着 `inspect or drop1 -> mode0` —— 箭头本身就是顺序说明。**

### 2.3 两句写入都是单字节

```c
static void change(const char *path, char value, i64 *written, i64 *closed) {
    i64 fd = op(path, 1);          // O_WRONLY
    if (fd < 0) { *written = fd; return; }
    *written = sc(64, fd, (i64)&value, 1, 0, 0, 0);   // write 1 byte
    *closed  = sc(57, fd, 0, 0, 0, 0, 0);             // close
}
```

**`image_mode_store` 的处理（`warm.c:191`）：取 `buf[0]` 一个字节，`value = c - '0'`，`if (value < IMAGE_MODE_NUM) image_mode = value;`。所以写 `"0"` → `image_mode = 0`（`IMAGE_MODE_NONE`）。**

---

## 3. 我的错误在哪一层

**我做的：**

```
printf '1' | dd of=/sys/power/image_drop
```

**内核回 `dropped wbi` —— **drop 确实生效了。****

**但我没写 mode=0。所以：**

1. **drop 之后镜像是 `\0\0...` 的 11 个零字段，`sector_size=0`**
2. **`RU_SIZE(n, 0)` 在 32 位下恒为 0，Loader2 拿到参数自矛盾的头部**
3. **而 mode 仍是 2，接下来任何一次正常关机都会把 `\0` 头当成"已有镜像"去 overwrite**

**更关键的是：我的 `drop` 决定本身就不是为了"改固件后冷启动"，而是为了"让 env.txt 生效"。这个目的从一开始就在 a7c2-tools 的语境里不存在。**

---

## 4. 两个项目真正的分歧点：他们改的是**标记**，我做的是**暴力清零**

### 4.1 A7M4 项目（`Erlla/sony-a7m4-custom-firmware`）

```python
old = read("0700_part_image/dev/nflasha5")
require(old[:4] == b"WBI2", "BASELINE", "Unexpected warm-boot marker")
write("0700_part_image/dev/nflasha5", old, b"WBI3" + old[4:])
```

**改 4 字节：`WBI2` → `WBI3`。其余全部不动。**

**这是**离线**做法：在刷入的固件包里改，随固件一起写进 eMMC。**

### 4.2 a7c2-tools

**用**运行时 sysfs** 做：`drop`（清零头）+ `mode=0`（关重建）。**

**这是**在线**做法：相机已经跑起来了，从用户态直接操作内核。**

### 4.3 我

**只抄了在线做法的第一步，没抄第二步。而且我的目的和这两个项目都不同。**

---

## 5. 所以正确的"清除 WBI"有两条路

### 路线 A：离线改标记（推荐，最安全）

**在固件包 / 分区镜像里把 `fVersion` 改成非法但结构合法的值，随刷写一起进 eMMC。**

```python
# nflasha5 offset 0，只改 4 字节
# WBI1 -> WBI9（或其他 Loader2 不认的标记）
data = bytearray(backup_nflasha5)
assert data[0:4] == b'WBI1'
data[0:4] = b'WBI9'
```

**优点：**

- **其余 52 字节头字段全部保留（`sector_size=512`、`sections=664`、`r_data_size` 都是对的）**
- **Loader2 判"不认识的标记" → 干净走冷启动**
- **不破坏任何数据，随时可以改回 `WBI1`**
- **不依赖相机当前能不能起来**

**这就是 A7M4 项目的做法（他们用 `WBI3`），只是我们要选一个 ZV-E10 这一代确实不认的标记。**

**注意：`WBI0` 是合法的最小快照标记，`prof` / `invl` 也是合法常量 —— 都不能用。`WBI9` 这类不在白名单里的才安全。**

### 路线 B：在线 drop + mode0（a7c2-tools 做法）

**前提：相机**当前**能起、能执行任意程序。**

```
echo 1 > /sys/power/image_drop     # 作废镜像（此时 mode 必须非 0）
echo 0 > /sys/power/image_mode     # 阻止关机重建
# 回读验证 mode == "0"
# 关机、拔电池 ≥1 分钟、装回、开机 → 干净冷启动
```

**优点：不用刷写。**

**缺点：两次写入不可分割，中途崩溃会留下"已 drop 但 mode 仍为 2"的危险状态。**

### 路线 C：改 cmdline（不动 flash）

真机 cmdline 已有：

```
warm.resume_drop=0
warm.boot_drop=0
warm.mode=2
```

**`warm.mode=0` 就能达到路线 B 的"不重建"效果，而且**每次启动都生效**，不需要运行时写 sysfs。**

**但这需要在 bootloader 层面改 cmdline，比改 4 字节难得太多。**

---

## 5bis. 重要更正：路线 B 在 ZV-E10 上**已经证伪**

**本文档早前把路线 B（`drop` + `mode0`）列为"a7c2-tools 的做法、可行"。这个表述必须收回。**

**事实：`image_drop=1` 在 ZV-E10 上**确实把相机搞砖了**（真机实测，2026-11-09）。**

**a7c2-tools 能用同一个原语，是因为三个条件我都不具备：**

| 条件 | a7c2-tools | ZV-E10 实况 |
|---|---|---|
| 机型 | 仅 A7C II（`ILCE-7CM2`），README 明确"其他机型**不能**使用，程序会拒绝连接" | ZV-E10，被拒绝的那一类 |
| Loader2 对全零头的处理 | 未公开，但从工具可用推断**会回退冷启动** | **实测不回退**：相机完全无响应 |
| mode=0 | 紧跟其后 | 我没有 |

**⇒ 关键差异不在 `mode0`，而在 **Loader2 对"全零 `fVersion`"的分支**。**

**对 ZV-E10 的 Loader2 反汇编（VA `0xFE047000` 起）可见四个**白名单**判定函数：**

```
0xFE049CCE  fVersion == "invl" ? 1 : 0
0xFE049CF0  fVersion == "prof" ? 1 : 0
0xFE049D10  fVersion == "WBI1" ? 1 : 0
0xFE049D30  fVersion == "WBI0" ? 1 : 0
```

**全是"等于某合法值吗"，**没有"不匹配就冷启动"的兜底**。全零头一个都不命中，行为取决于调用方 —— 实测结果是**卡死，连 USB 都不枚举**。**

**⇒ 所以"a7c2-tools 这样做能行"**不能**推出"ZV-E10 这样做能行"。这是机型相关行为，我把它当成了通用行为。**

**⇒ 路线 B 对 ZV-E10 必须标为**禁止**。只有路线 A（离线改 4 字节标记、保留其余字段）是安全的。**

## 6. 为什么 `boot_drop=1` 不是好选择

**内核里 `boot_drop` 的语义（`warm.c:1304`）：**

```c
if (wbi_boot_drop && wb_prev_drop != wb_default_device) {
    wb_prev_drop = wb_default_device;
    __wbi_drop(wbi_sectorbuf, DEFAULT_SECTOR_SIZE);   // ← 同样写 512 字节 0
}
```

**⇒ `boot_drop=1` 每次注册 WBI 设备都自动清零 512 字节 —— **和我的操作一模一样**，同样留下 `sector_size=0`。**

**⇒ 真机当前是 `boot_drop=0`，索尼自己也把它关掉了 —— 说明这**不是**给用户用的正常路径，而是工厂/调试用的。**

**⇒ 社区那位说"每次启动都清除，代价有点大，不清楚闪存寿命"—— 他的顾虑是对的：每次启动写 512 字节，闪存磨损 + 每次都是暴力清零。**

---

## 7. 给 ZV-E10 的具体建议

**现在相机已救回（`nflasha5` 已写回）。要真正解决"改固件不生效"的问题，应该：**

1. **不要再用运行时 `image_drop`。** 它是我踩的坑，也是社区被证伪的方案。
2. **离线改 4 字节标记**：把备份的 `nflasha5.img` 第 0-3 字节从 `WBI1` 改成 Loader2 不认的值，用 fastboot 写回。
3. **或者更彻底：刷一个 `warm.mode=0` 的 bootloader。** 但风险高。

**验证冷启动成功的方法（a7c2-tools 的做法）：记录 `reload_before_boot_id_sha256`，重启后比对 `boot_id` 变化。**

```python
# devicejob.py:346
self.state['reload_before_boot_id_sha256'] = sha(runtime['boot_id'])
# 重启后 499 行比对
same_boot = (sha(boot) == self.state['reload_before_boot_id_sha256'])
```

**⇒ 这比"看菜单有没有变化"可靠得多：`boot_id` 变了 = 真的冷启动了。**

---

## 8. 一句话总结

**`a7c2-tools` 清 WBI 的完整操作是两个 sysfs 写入：`image_drop=1` 然后 `image_mode=0`，顺序不可反，因为 `_wbi_drop()` 遇到 `image_mode==0` 会提前返回，而 `cxd_create_warmbootimage()` 遇到 `image_mode==0` 会跳过重建。**

**我只做了第一步，这等价于"作废了镜像但不阻止它被重新生成"—— 而且作废方式（清零整个扇区）比 A7M4 的改 4 字节更暴力，留下 `sector_size=0` 这种参数矛盾状态，Loader2 行为未定义。**

**最安全的做法是路线 A：离线把 `WBI1` 改成非法标记，保留其余全部字段。**

---

## 证据清单

| 事实 | 来源 |
|---|---|
| a7c2-tools 的 drop 命令 | `src/fl23/native/wbi.c:48` |
| 紧随其后的 mode0 | `src/fl23/native/wbi.c:51` |
| "drop1 -> mode0" 顺序注释 | `src/fl23/native/wbi.c:1` |
| mode=0 阻止关机重建 | 内核 `warm.c:1217` `WBI_MODE` |
| mode=0 使 drop 提前返回 | 内核 `warm.c:398` `if (!image_mode) goto out` |
| `image_mode_store` 单字节处理 | 内核 `warm.c:191-208` |
| A7M4 只改 4 字节标记 | `Erlla/sony-a7m4-custom-firmware` `a7m4/firmware.py:252` |
| `boot_drop=1` 同样是清零 | 内核 `warm.c:1304` |
| 真机 boot_drop=0 / mode=2 | `/root/fw/real_cmdline.txt` |
| boot_id 验证冷启动 | a7c2-tools `devicejob.py:346,499` |

未验证：Loader2 对 `fVersion` 的完整白名单（只能确认 `WBI1`/`WBI0`/`prof`/`invl` 是已知常量，`WBI2`/`WBI3` 来自 A7M4，ZV-E10 这一代具体拒绝哪些未知）。

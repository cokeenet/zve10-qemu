# 通过 initrd 注入 LD_PRELOAD

## 一句话

改 `/system/INITRD.IMG` 里的 `/sbin/init`，让它 `putenv("LD_PRELOAD=/z.so")`，
库直接嵌在 initramfs 里 —— **不依赖任何分区挂载时机**。

## 为什么其他路都不通

| 尝试 | 结果 | 原因 |
|---|---|---|
| `/etc/ld.so.preload` | ❌ | `im.elf` 启动时 `/etc` 还没挂载完 |
| 改 `/usr/bin/im.elf` | ❌ | 重启时被强制还原（连 1 字节的假文件也还原） |
| 改 `nflasha7` 的 `/sbin/init` | ❌ | **真正跑的是 `/system/INITRD.IMG` 里的 init** |
| 杀 `im.elf` 重启 | ❌ | **系统卡死**（实测） |
| `bootin` 的 `preload` 白名单 | ⚠️ | 能过白名单，但 `bootin` 执行在 `/etc` 挂载前 |

## 启动链（已逐个确认）

```
内核
  ↓ 解压 /system/INITRD.IMG 到内存
initramfs 的 /sbin/init    ← ★ 就是这个，不是 nflasha7 上的
  ↓
init 的 main（0x9A10）：
  0x9A1C  mount_partition(/proc)
  0x9A26  open /proc/snsc_boot_time
  0x9A3C  putenv 循环 ← ★ 我们在这里注入 LD_PRELOAD
  0x9A48  set_stack_size
  0x9A58  check_oreore1
  0x9A60  insmod_drivers
  ...
  0x9AF6~0x9B74  挂载 /usr /tmp /etc /system /setting /log /lens /cert
  ...
  0x9F3C  run_bg_process("/usr/bin/bootin.elf") ← bootin 启动
         ↓
       bootin fork + execve("/usr/bin/im.elf")
         ↓
       im.elf（tick 104，约 5.2 秒）→ 继承 LD_PRELOAD → ld.so 加载 /z.so ✓
```

**关键：init 的挂载列表里【没有 `nflasha7` → `/`】，所以 `/` 一直是 initramfs（内存盘）。**

**⇒ 把库嵌进 initrd，init 在 `0x9A3C` 执行 putenv 时 `/z.so` 已经可读。**

## 补丁详情

```c
// init 的 main 开头有一个 putenv 循环（0x9A3C-0x9A46）
// 它遍历 .rodata @ 0xA35C 的数组：
//   [1] HOME=/
//   [2] PATH=/sbin:/bin:/usr/sbin:/usr/bin
//   [3] TOPRC=/etc/toprc
//   [4] MALLOC_OPTIONS=gfffffffff
//   [5] MALLOC_MMAP_THRESHOLD_=32768
//   [6] MALLOC_TRIM_THRESHOLD_=32768
//   [7] LT_ABORTREASON=0        ← 17 字节
//   [8] NULL

// 把 [7] 换成等长的 "LD_PRELOAD=/z.so"：
//   vaddr 0xAE2F: "LT_ABORTREASON=0\0" → "LD_PRELOAD=/z.so\0"
//   都是 17 字节，不改任何指针
```

**md5:**
```
原始 init: 0ff6dd725d90ebd67ecc44839be58f3f
补丁 init: 7f6063840c6d99cd0461e93c5ce104d0
```

## 库放哪

**initramfs 里**（不是分区文件系统）：

```
/z.so       libnewmenu.so（135 KB）
/z_safe     空文件（safe mode 开关，第一阶段验证用）
```

**initrd 剩余空间：8,192 B。**（库 135,380 B + initrd 原有 1.9 MB，共 2.1 MB / 2.18 MB）

## 两阶段部署

### 阶段 1：验证库能加载（零风险）

`initrd_safe.img` 里带 `/z_safe`，库检测到它后：

```c
if (access("/z_safe", F_OK) == 0) {
    nm_log("NM SAFE MODE: library loaded OK, hooks DISABLED\n");
    dl_iterate_phdr(nm_phdr, NULL);
    nm_log("NM safe-mode object=%#lx views=%#lx\n", object_base, views_base);
    return;      // 不装任何 hook
}
```

**预期 `/log/newmenu.log`：**
```
NM ui pid=xxx lock=x
NM SAFE MODE: library loaded OK, hooks DISABLED
NM safe-mode object=0x... views=0x...
```

**看到这些 = 库成功进 `im.elf`，方案成立。**
**没看到 = 库加载失败，但相机仍正常启动（safe mode 不碰菜单）。**

### 阶段 2：装 hook

换成 `initrd_hook.img`（无 `/z_safe`），库走正常路径。

## 构建

```sh
# 阶段 1
SAFE_MODE=1 python3 build_initrd.py    # → initrd_safe.img

# 阶段 2
SAFE_MODE=0 python3 build_initrd.py    # → initrd_hook.img
```

## 部署

```sh
# 1. 备份原始 initrd（关键！）
cp /system/INITRD.IMG /etc/INITRD.IMG.orig

# 2. 推新的（2.18 MB，需分块）
#    用 tools/payload/chunk_push.py

# 3. 重启
```

## 恢复

```sh
# 如果相机能起来但有问题：
cp /etc/INITRD.IMG.orig /system/INITRD.IMG
reboot

# 如果相机完全起不来（init 崩了）：
#   必须用 fastboot（Pico + stage1/stage2）
#   改 nflasha3（/system 分区）的 INITRD.IMG
#   备份在 /root/fw/zve10_backup/nflasha3.img
```

## 文件

```
initrd_safe.img   2,183,168 B  阶段 1（带 /z_safe）
initrd_hook.img   2,183,168 B  阶段 2（无 /z_safe）
build_initrd.py   构建脚本
patch_init.py     仅打 init 补丁（不建整盘）
```

## 关键数据备份

```
/tmp/cam_initrd.img                     原始 initrd（从相机提取）
/root/fw/zve10_backup/nflasha3.img      /system 分区完整备份（48 MB）
/root/fw/ref_e10/root/sbin/init         nflasha7 上的 init（同 md5，但不用）
```

## 为什么只改 17 字节而不扩段

- `.rodata` 末尾的 424 字节空隙是 `.ARM.exidx` 的内容，**不是空闲的**
- `.data` 之前虽然看起来有 33 KB 空隙，但**在 `LOAD#1` 的 filesz 之外**，不可访问
- **⇒ 17 字节等长替换是最干净的方案，不改段表、不改指针、不影响任何逻辑**

## 完整验证清单（静态）

| 项 | 结果 |
|---|---|
| `0xAE2F` 在 `.rodata`（0xA35C-0xB1F0）内 | ✅ |
| `0xAE2F` 只在 putenv 数组里出现 1 次 | ✅ |
| 循环 `0x9A3C-0x9A46` 会处理数组 [1]~[7] | ✅ |
| putenv（0x9A3C）早于 bootin 启动（0x9F3C） | ✅ |
| 17 字节等长替换，不改任何指针 | ✅ |
| `/z.so` 在 initramfs，不依赖挂载 | ✅ |
| 库依赖 libdl/pthread/rt/c 都在 `/lib` | ✅ |
| initrd 空间够（142 KB 可用，库 135 KB） | ✅ |
| 原始 initrd + 分区备份齐全 | ✅ |

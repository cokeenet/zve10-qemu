# 两个构建目标

```
sh build.sh slim    # -> libnewmenu.so      真机
sh build.sh full    # -> libnativeentry.so  QEMU 验证
```

| | slim | full |
|---|---|---|
| 产物 | `libnewmenu.so` 139,744 B | `libnativeentry.so` 214,228 B |
| 源码 | `mini_main.c` + `native_entry.inc` + `transport_nop.inc` | `full/observer-derived.c` + `full/menu_shell.c` |
| dlopen hook | 有 | 有 |
| 菜单插入 | 有 | 有 |
| headless 渲染 | 无 | 有 |
| 串口协议（CAPTURE/KEY/ACK） | 无 | 有 |
| 用途 | 相机 | 在 QEMU 里证明能力 |

## 为什么不是一个

`native_entry.inc` 在 `#include "transport.inc"` 和 `"transport_nop.inc"` 之间切换，
决定要不要把 headless 渲染管线和串口协议链进来：

- 相机上 UI 自己绘制，不需要 headless 渲染，也没有 host 来发 `CAPTURE`，
  所以用 nop 版本，库小 74 KB。
- QEMU 里需要把帧抓出来，所以用完整版。

两份源码都在这个目录里，`full/` 是完整版需要的文件集合。

## 已修复，勿回退

**1. `ne_modules_installed` 必须置 1**
`ne_init_wrapper` 用它做门禁：

```c
if (ne_modules_installed && !ne_installed && caller == views_base + 0x301921)
    ne_add_row(root);
```

不置 1 则函数体不可达 —— 编译器证据：该函数只有 20 字节，应为 176。

**2. `ne_caution` 必须赋值**
`ne_add_row` 在 vptr 断言和计算 `clone`/`construct_properties` 目标时用它三次。
不赋值会在 `-3` 失败或跳到裸偏移（如 `0xb8eafd`）。

**3. `ne_view_handle` 必须赋值**
`ne_find_symbol` 是：

```c
return ne_view_handle ? dlsym(ne_view_handle, name) : NULL;
```

不赋值则 `ne_add_row` 里所有符号查找返回 NULL，`version` 为 NULL，
第一道检查直接 `-1`。表现为：

```
NE_VERSION {"node":0,"vptr":0,"properties":0,"last":-1}
NE_ERROR phase=append_row status=-1
```

**4. 字体 hook 的 `expected` 必须是 12 字节**

`ne_hook` 的签名是：

```c
static int ne_hook(uintptr_t address, const unsigned char expected[12], ...)
{
    if (memcmp((void *)address, expected, 12)) return -1;
```

但调用处一度只给了 8 字节字面量：

```c
ne_hook(object_base + 0x626538,
        (const unsigned char[]){0x90,0xb5,0x83,0xb0,0x00,0xaf,0x04,0x46}, ...)
```

后 4 字节读的是栈上未初始化内存，`memcmp` 必然失败，于是

```
NM warn: string_id hook failed
NM warn: string_chars hook failed
```

补齐为 `libObj.so` 在该偏移的真实 12 字节（`0x626538` 后接 `38 46 ea f7`，
`0x6266d0` 后接 `04 46 0d 46`）后警告消失，`NM installed: 1`。

教训：`expected` 是按 12 字节比较的，字面量少给字节不会报错，只会静默失败。

**5. 不要用 `setitimer(ITIMER_REAL)`**
它会往 im.elf 主线程注入 SIGALRM，菜单卡死；且信号处理器里跑 `dl_iterate_phdr`
不安全。重试改为搭在每次 `dlopen` 结束时。

**6. `dlopen` 必须有四重兜底**
`dlvsym` / `dlsym` / `libdl` handle via `RTLD_NOLOAD` / `__libc_dlopen_mode`。
返回 NULL 会破坏 im.elf 里所有 dlopen。

**7. `CautionConfig.so` 不经过 dlopen**
它是 `viewUnified2.so` 的 `DT_NEEDED`，靠每次 dlopen 结束时的通用重试覆盖。

## 不要同时 preload 两个 hook 库

两个库都导出 `dlopen`/`dlsym`，且都没有可用的构造函数
（`libnativeentry.so` 的 `.init_array` 只有 `frame_dummy`）。
`LD_PRELOAD` 里排前面的赢得符号，后面那个被加载但永不启动。

实测：

```
LD_PRELOAD=/usr/lib/libnewmenu.so:/usr/lib/libnativeentry.so
NM 行: 354    NE 行: 0
```

反过来则镜像。

## 验证记录

**slim 在路线 B**（`kbuild-ui2` + 我们自己的 initramfs）：

```
NE_VERSION      {"last":6}
NE_TREE_ADDED   {"old_count":5,"new_count":6}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          {"title":50265857}     == NE_TITLE 0x02FEFF01
NE_ERROR        0
```

**full 在路线 A**（`kbuild-ui` + `native-ui.cpio`），三帧全部字节等同上游：

| 帧 | sha256 | 上游对应 |
|---|---|---|
| 父页 | `93ee3a7e1428a374…` | `visible-parent.png` |
| ENTER 后（A7C2 菜单） | `41aec88e71396669…` | `visible-new-menu.png` |
| MENU 后（返回父页） | `93ee3a7e1428a374…` | `visible-parent.png` |

原始 RGBA4444 也对上 `main-review.json` 的 `parent_raw_sha256`
和 `host_category_sha256["7"]`。

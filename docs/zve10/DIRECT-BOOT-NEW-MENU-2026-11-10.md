# 直接开机就是新菜单：不经过老菜单，也不需要按键

2026-11-10。回答"能不能把老菜单删掉，开机直接就是新菜单"。

## 结论

**可以，且已经在 QEMU 里做到了。** 两次独立运行，开机第一帧就是 A7C2 新菜单，
没有老菜单、没有 Sony 框架、没有任何按键。

## 之前的 route A 是不是"从老菜单进入"

**是。** route A 的机制是在原厂父页（`セットアップ5`）里注入第五行 `New Menu`，
再由 ENTER 键触发打开。证据：

```text
NE_TREE_ADDED {"old_count":5,"new_count":6}      ← 往老菜单里加一行
NE_INPUT_CONSUMED {"function":5,"overlay":1}     ← ENTER 之后新菜单才出现
NE_INPUT_CONSUMED {"function":6,"overlay":0}     ← MENU 返回老菜单
frame-001（父页）= 老菜单，frame-after-ENTER = 新菜单
```

所以 route A 满足"能进新菜单"，但**不满足你这次要求的"开机直接是新菜单、老菜单不要"**。

## 直接开机方案的构成

不需要 Sony 框架，也不需要 `libnativeentry` 的注入逻辑。用的是同一套 A7C2 原资源
在 guest 侧用纯 C 合成的一页菜单：

```text
guest/init.c        自持进程：mlock 614400B 像素缓冲，mmap 后按段表上报物理地址
guest/menu_shell.c  193 行纯 C 渲染器，只依赖 original_assets.h
                    （8 个分类、44 条记录、16 张分类图标、原字体）
```

它不 dlopen `appFw.so`、不启动 `bootin.elf`、不加载任何 Sony 模块：

```text
MENU_BOOT {"schema":1,"pid":1,"modules":0,"sony_ap_started":false,
           "backend":"owned-empty-menu"}
```

**开机第一件事就是 `menu_init()` → `menu_render()`，然后直接上报帧。**

## 实测（两次独立运行）

```text
initramfs  direct-menu.cpio  128,000 B  sha256 66f1c4c46cec4f3d42f779f15821707e78b517cbf76f2dcd1495ed1b6d8f2201
启动       -kernel kbuild-ui Image，rdinit=/init，无 preload
```

| 项 | run1 (direct2) | run2 (direct3) |
|---|---|---|
| raw sha256 | `0c7e1c6682ec4cde4c56e8e2b2b617b7aa8d874844335f9d7fd1e4cee53dbd9b` | **同左** |
| 字节数 | 614400 | 614400 |
| 非零字节 | 556163 | 556163 |
| 唯一色 | 123 | 123 |
| 行相似 | 477/479 | 477/479 |
| 段表基址物理地址 | 4706304 = `0x47D000` | 4706304 = `0x47D000` |
| 段数 | 78（合计 614400 B） | 78 |
| 分页取回 | ok=78 fail=0 | ok=78 fail=0 |

**两次运行哈希完全相同，物理地址相同。**

画面：标题 `设置`，右上 `0/0`，左侧图标栏（星／房＋相机／相机／AF·MF＋相机／播放／
地球／箱子），右下 `MENU 关闭`。行相似度 477/479、唯一色 123 —— 结构化的菜单，
不是噪声。

## 与 route A 的 ENTER 帧是同一张

```text
直接开机第一帧       0c7e1c66…（raw）
route A ENTER 之后   0c7e1c66…（raw）
逐像素差             0 / 307200
```

也就是说：**直接开机省掉的是"先看老菜单、按 ENTER"这两步，看到的是同一个新菜单页。**

## 老菜单到底还在不在

- 在**直接开机**这一版里：**完全不存在**。guest 里没有任何 Sony UI 代码运行。
- 在 **route A** 那一版里：老菜单仍在，新菜单是从它里面进入的。

两套东西是独立的，互不影响。

## 代价与边界（必须说清）

这套直接开机只是**一页静态菜单壳**：

- 只有分类列 + 项目列 + 焦点高亮，`menu_key()` 支持上下左右/ENTER/MENU。
- **没有**照着 A7C2 真实业务：没有拍摄主界面、没有设置项的实际功能、没有相机业务。
- 它不是把 ZV-E10 变成 A7C2，只是把 A7C2 的菜单页画出来。
- **这是 QEMU 内验收，不是真机验收。**

## 复现

```text
tools/zve10/qemu/run_direct_menu.sh         单实例、收尾清扫
tools/zve10/qemu/direct_host.py             串口 MENU_FRAME + QMP pmemsave 取帧
tools/zve10/qemu/direct-menu-archive.spec   cpio 清单
docs/zve10/direct-menu-proof/direct-menu.cpio
```

## 约束遵守

```text
单实例  systemd-run --unit=zve10-direct2/3 --collect --property=MemoryMax=4G
清扫    systemctl stop 'zve10-*' + /proc/PID/exe basename
收尾    qemu-system-arm 残留 = 0
产出    仅写 /root/fw/oss
```

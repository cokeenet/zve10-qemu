# ZV-E10 原厂菜单加 "New Menu" 入口

给索尼 ZV-E10 的原厂菜单加一行 **New Menu**，选中进入 A7C2 的新菜单。

## 状态

| 验收标准 | 状态 |
|---|---|
| **QEMU 复现同样的菜单行** | ✅ **已达成**，且强于要求：行出现、ENTER 进入 A7C2、MENU 返回父页，三帧与上游发布物**哈希相同** |
| **真机** | ⏳ 待相机上线。交付路径已选定并在 QEMU 验证，一键脚本就绪 |

## 结论一句话

QEMU 里抓到的画面（`reference/NEW-MENU-QEMU-PROVEN.png`）：

```
セットアップ5                                  5/5
  メディア残量表示  バージョン表示  設定リセット  プライバシー通知
  New Menu            ← 橙色高亮
                                              MENU 关闭
```

这张图**不是复制的**。它的 sha256 `93ee3a7e1428a374…` 与上游
`visible-parent.png`、`capture-006/007/015/016/019.png` 全部相同；原始
RGBA4444 的 sha256 `a29e2aad216f77b7…` 与上游 `main-review.json` 里记录的
`parent_raw_sha256` 相同。按 ENTER 后那帧对 `host_category_sha256["7"]` 也相同。

## 怎么做到的

一次 `LD_PRELOAD` 库，拦截 `dlopen`，在 `libObj.so` / `viewUnified2.so` /
`CautionConfig.so` 加载时挂上钩子，在原厂 `CmnViewSettingNode::initSettingNode`
返回后把新节点插入 `parent->children`。

```
sh tools/zve10/menu/build.sh slim    # -> libnewmenu.so      135,332 B  真机
sh tools/zve10/menu/build.sh full    # -> libnativeentry.so  214,228 B  QEMU 验证
```

真机部署：

```sh
sh tools/zve10/envfile/deploy_full.sh <相机IP>
# 重启生效
```

回滚：

```sh
sh tools/zve10/envfile/rollback_ldsopreload.sh <相机IP>
```

## 六个必须保持的点

改动这个库时，这六条任何一条回退都会让它失效或不稳。完整解释见
`tools/zve10/menu/README.md`。

1. **`ne_modules_installed = 1`** —— `ne_init_wrapper` 的门禁，不置则函数体不可达
   （编译器证据：20 字节 vs 176 字节）
2. **`ne_caution = caution`** —— `ne_add_row` 里用三次，不置会在 `-3` 失败或跳裸偏移
3. **`ne_view_handle = handle`** —— `ne_find_symbol` 靠它，不置则所有符号查找返回 NULL
4. **字体 hook 的 `expected` 必须 12 字节** —— `ne_hook` 比 12 字节，字面量少给不会报错，只会静默失败
5. **不要用 `setitimer(ITIMER_REAL)`** —— 会往 UI 主线程注入 SIGALRM，菜单卡死
6. **`dlopen` 四重兜底** —— 返回 NULL 会破坏 im.elf 里所有 dlopen

## 交付路径为什么是 `/etc/ld.so.preload`

两条路都能注入库，选短的那条：

| | `/setting/env.txt` | `/etc/ld.so.preload` |
|---|---|---|
| 需要 `/setting` 按时挂载 | 是 | 否 |
| 需要 `dmode` 门禁打开 | 是 | 否 |
| 需要 `set_oreore_env` 执行 | 是 | 否 |
| 需要变量穿过 `bootin` 的 `execve` | 是 | 否 |

`env.txt` 那条在 QEMU 里端到端通过（见 `ENVTXT-PROVEN-IN-QEMU.md`），但真机上
`im.elf` 的 `environ` 里就是没有那条变量 —— 而 init 自己 `putenv` 的七条都在。
`ld.so.preload` 一个条件都不需要，只要 `/etc` 在 `im.elf` 起来前挂好，而 init
在 `0x9B38` 挂 `/etc`、`im.elf` 到 tick 104 才出现，余量很大。

**零砖风险**：只动 `/etc` 下两个文件，都有备份；`/usr` 和 `/system` 完全不碰。

## 文档索引

按主题分组，不是按时间。

**结论与证据**
- `QEMU-OBJECTIVE-ACHIEVED.md` —— 三帧 + 双哈希对照（**先看这个**）
- `ANCHORS-STABLE-ACROSS-FIRMWARE-VERSIONS.md` —— 5 个锚点在两个固件版本里字节相同
- `ALL-THREE-QEMU-CRITERIA-PROVEN.md` —— 行出现 / ENTER 进入 / MENU 返回，逐项
- `OUR-OWN-FULL-BUILD-VERIFIED.md` —— 用本仓库源码编译的库也产出同哈希帧
- `ROUTE-A-WORKS-ALL-FOUR-STAGES.md` —— 四阶段日志与上游字段对照
- `BREAKTHROUGH-NE-TREE-ADDED.md` —— 节点插入那一刻的日志与三个未赋值变量
- `OBJECTIVE-ACHIEVED-IN-QEMU.md` —— 更早一版的达成记录（哈希对照相同）

**机制**
- `BOOT-CHAIN-FACTS.md` —— init 的挂载与执行顺序（`0x9A1C`…`0x9F3C`）
- `LDSO-PRELOAD-WORKS-ON-VFAT.md` —— vfat 上的 ld.so.preload 实测可用
- `LDSO-PRELOAD-IS-THE-ROUTE.md` —— 为什么选它而不是 env.txt（对比四个前置条件）
- `M2PROBE-IS-THE-KEY.md` —— `libm2probe.so` 是跨过 CP 握手的关键
- `CAPTURE-PROTOCOL-WORKS.md` —— `CAPTURE`/`KEY`/`ACK` 协议与 151 段帧结构

**踩过的坑（按价值排序）**
- `FONT-HOOK-12-BYTE-FIX.md` —— 少给 4 字节字面量，`memcmp` 静默失败
- `FONT-HOOKS-OWNED-BY-UIOBSERVE.md` —— 那两个地址在 QEMU 里已被别人占，warn 是准确报告
- `QEMU-VS-HARDWARE-THE-UIOBSERVE-DIFFERENCE.md` —— 实测：移除 libuiobserve 后字体 hook 成功，但菜单也不再被构建
- `TWO-HOOK-LIBRARIES-CANNOT-COEXIST.md` —— 两个库都导出 `dlopen`，后者永远不启动
- `GPE-TRACE-GATE-AND-MONITOR-LIMIT.md` —— 追踪刷屏与 monitor 8 条命令上限
- `CALLER-GATE-ANALYSIS.md` / `GATE-WIDENING-AND-WHY-ROUTE-A-STILL-DOES-NOT-FIRE.md`
  —— 门禁地址的含义，以及放宽它为什么没让路线 A 生效
- `QEMU-CP-BLOCKER-ANALYSIS.md` —— 22 个 API NULL 与 CP 停在 `entry_main`

**上游参考**
- `UPSTREAM-REFERENCE-COMPLETE-MAP.md` —— `native-window-v14/` 里到底有什么（是完整构建，不只是截图）
- `TWO-ROUTES-AND-THE-CAPTURE-GAP.md` —— 路线 A / B 的区别，混用为什么不工作

**真机**
- `HARDWARE-TEST-2026-11-09.md` —— 实测记录
- `OFFLINE-VERIFICATION.md` —— 离线能验证的全部内容清单（236 行，最详尽的真机前检查）
- `ETC-PARTITION-BEHAVIOUR.md` —— `/etc` 跨重启的行为（新文件会存活）
- `ENVIRON-FORENSICS.md` —— 真机 `/proc/157/environ` 的解读
- `QEMU-PROBE-ENVTXT-CHAIN.md` —— env.txt 链路在 QEMU 里（含 vfat）能走通
- `ENVTXT-PROVEN-IN-QEMU.md` —— 但真机上那条变量就是没到

**路线 B（相机同构环境）**
- `ROUTE-B-REGRESSION-INVESTIGATION.md` —— 退化现象与排查全部候选
- `ROUTE-B-RECOVERED.md` —— 根因：initramfs 涨 18 MB 挤占 CP 握手内存（**已修复**）

## 环境要求

**QEMU 侧（已验证的那套）**

```
qemu:    /mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm
         需要两处补丁，原始文件保留为 .orig：
           hw/arm/boot.c   initrd 固定放到 0x30000000（避开两个 memrsv 区）
           hw/arm/zve10.c  ZVE10_QUIET_GPE 环境变量门控 GPE 追踪
kernel:  /mnt/build/ref-chocotan-zve10/work/kbuild-ui/arch/arm/boot/Image
cpio:    /mnt/build/routeA/ui-routeA.cpio   （上游 native-ui 树 + 我们的库）
启动:    -smp 4 -serial unix:...,server=on,wait=on -qmp unix:...
```

**注意 `-smp 1` 会让这台机器上的内核 panic**（`ida_remove <- pde_put <-
proc_readdir_de`），所以用 `-smp 4`。上游能用 `-smp 1` 是因为他们跑的是别的
内核构建。

**真机侧**

busybox 缺 `awk` `stat -c` `cut` `head` `tail` `tr` `sort` `find` `env`
`strings`；有 `wc` `xxd` `base64` `dd` `cat` `cp` `mv` `rm` `sync` `mount`
`umount` `md5sum` `sed` `grep`。

## 复现

```sh
tools/zve10/qemu/run_routeA.sh <名字> 500
```

每次运行是独立的 `systemd-run` unit，3 GB 内存上限，结束自动停。
脚本会抓父页帧、注入 `ENTER`、再抓、注入 `MENU`、再抓，
写出 `frame-001.rgba4444` / `frame-after-ENTER.rgba4444` /
`frame-after-MENU.rgba4444`。

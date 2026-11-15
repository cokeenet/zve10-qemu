# 相机侧备份（已全部拉回本地，相机 /log 已清空）

拉到本地时间：2026-11-15 23:04-23:07
38 项，共 9.8 MB（`CautionConfig.factory.so` 因 PULL 8MB 上限无法完整拉取，
但本机固件树 `fw/COMPARE/extracted/ZVE10/usr/lib/CautionConfig.so` 与之同 sha，
已用 `5ad8746a229da259fe00…` 核对，无需重复保存）

## 一、相机 /log 上现在只剩这些（共 10 项，4.4 MB）

```text
AccessLog.dat       2,097,220   相机自己写的访问日志
AccessLog.bak       2,097,220   相机自己写的
Backup_live.bin     1,235,836   相机当前设置库快照
im.cmdline              1,918   im.elf 启动参数（复刻用）
im.cwd                     42   /usr/share/app
im.environ                 47   im.elf 环境变量
ldp.orig                   20   /etc/ld.so.preload 原值
mode-dmode.orig             1   dmode 原值 "3"
mode-preload.orig          62   preload 原值
zve10_agent.log         9,003   agent 日志
```

**⇒ 除了 `im.*` 三个（我为了复刻 im.elf 启动留的，共 2 KB），其余全是相机自己的。**

## 二、本地备份清单（36 个文件）

### 救命备份（相机状态回滚用）

| 文件 | 大小 | sha256 前缀 |
|---|---|---|
| `.Backup.bin.orig` | 1,235,836 | `0c1c67588a30532a…` |
| `.beeplist.txt.orig` | 5,239 | `c43866bb9565d201…` |
| `setting-backup.tar` | 7,583,232 | `bac657bf453ed344…` |
| `.Dslr_Emnt_Shutter_Sha2/3/4.bin.orig` | 1216/1856/1472 | `957ae893…` / `e932f708…` / `0ef38673…` |
| `.Dslr_Emnt_10FPS_Kasha{,2,3,4}.bin.orig` | 1216/1216/1856/1472 | 同上 |
| `.soundlab-*.backup` ×2 | 1216 ×2 | `957ae893…` |

### 配置原值（几百字节，最常用）

| 文件 | 大小 | 内容 / sha |
|---|---|---|
| `mode-dmode.orig` | 1 | `3` / `4e074085…` |
| `mode-preload.orig` | 62 | `/usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so` / `dd6a9d7f…` |
| `ldp.orig` | 20 | `/etc/zve10_agent.so\n` / `e5237164…` |
| `ld.so.preload.bak` / `ldp.bak` | 20 ×2 | 同上（三份重复） |

### im.elf 启动复刻（关键）

| 文件 | 大小 | 内容 |
|---|---|---|
| `im.cmdline` | 1,918 | `/usr/bin/im.elf debug s g r u l i a lazy-global` |
| `im.cwd` | 42 | `/usr/share/app` |
| `im.environ` | 47 | HOME=/ TERM=linux PATH=... MALLOC_OPTIONS=gfffffffff 等 |

### 本轮实验证据（★ 关键）

| 文件 | 大小 | 说明 |
|---|---|---|
| `nm-probe3.log` | 31 | ★ **`P3 pid=355 tid=355 comm=im.elf`** —— 库成功进 im.elf 的唯一证据 |
| `nm-step1.log` | 30 | 第一版探针输出 |
| `nm-step2.log` | 36 | 第二版（带 dmode 自动回滚）|
| `bootin-run.log` | 54 | bootin 重执行输出（GLES/DMP_2D 初始化）|
| `nm_probe3.so` | 5,496 | 成功的探针二进制（源码在 `port/menu/nm_probe3.c`）|
| `nm_step1.so` / `nm_step2.so` | 5,480 ×2 | 前两版 |
| `fsinfo.log` | 195 | 分区 statfs（不可用，statfs64 未实现）|
| `freeze-test.log` | 32 | ptrace 冻结试验（**导致过重启**）|
| `tids.list` | 805 | im.elf 线程列表（201 个）|
| `ptinject{,2,3}.elf` | 13 K ×3 | ptrace 注入试验程序 |
| `survey.elf` / `sysread.elf` | 8.6 K ×2 | 系统探测 |

### 原厂对照

| 文件 | 大小 | 说明 |
|---|---|---|
| `Backup_live.bin` | 1,235,836 | 相机当前设置库（与 .Backup.bin.orig 不同 sha，是 live 版）|

## 三、重要的一个发现（清理时暴露的）

```text
/log/CautionConfig.factory.so  sha = 5ad8746a229da259…
/usr/lib/CautionConfig.so      sha = 14f42cc09ee1b9dd…   ← **与 /log/cc_patched.so 相同**
本机固件树 fw/COMPARE/extracted/ZVE10/usr/lib/CautionConfig.so sha = 5ad8746a229da259…  ← 原厂

⇒ **相机上的 /usr/lib/CautionConfig.so 是"5GHz 实验"替换过的版本**，
   不是原厂。
⇒ 原厂版本本机已有；将来若要还原 /usr，用固件树那份。
⇒ 但 /usr 是 ro ext2，说明它当初是通过某种手段写进去的（或改了 imghdr）。
```

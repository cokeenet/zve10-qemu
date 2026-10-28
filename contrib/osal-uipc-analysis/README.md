# uipc 观测工具集（ZV-1F 新菜单 / CXD90045 QEMU）

用于在**不需要真机**的情况下，看清 Sony 相机 App 到底在跟谁通信、卡在哪个调用上。
本目录是 2026-10-28 那轮排查的完整产物，**结论见 [`HANDOFF_20261028.md`](HANDOFF_20261028.md)**。

## 这些工具能回答什么

- App 向 uipc 发了哪些请求（按 cmd 分类、按目标 qid 分类）
- 哪个调用**进了内核但永远不返回**（用全局序号精确配对，不靠计数差）
- 某个 ioctl 的用户结构与内核侧 handler 的确切形态（配合反汇编）
- 内核日志（klog）怎么从物理内存里读出来

## 文件

| 文件 | 作用 |
|---|---|
| `core-sync-trace.c` | ★ `LD_PRELOAD` 探针：拦截 `ioctl` + 补 `libCoreSync` 三符号（只占一个 preload 槽） |
| `run-klog.sh` | 用 QMP `pmemsave` 从物理 `0x30000000` 读 klog（含 8 MB 缓冲区的正确配置） |
| `run-long-window.sh` | 长窗口运行 + host CPU / 串口采样 |
| `run-plain-control.sh` | 无 gdb / 无 QMP 的对照运行（排除"观测手段打断 App"） |
| `run-breakpoint-dump.sh` | 用 `scan-dump.gdb` 下断点并 dump 寄存器/栈 |
| `rebuild-with-trace.sh` | 重建 `/usr` 树 → 装入探针 → 打补丁 → 重新打包 → 跑一轮 → 抽日志 |
| `bf-dump.gdb` / `scan-dump.gdb` | gdb 脚本：扫签名定基址、下断点、命中时打印 r0–r15 + 栈扫描 |

## 使用前必改

所有脚本里写死了作者的路径，用前改这两处：

```bash
ROOT=/mnt/g/WorkBuddy/zve10        # 你的工作区
QEMU=$HOME/qemu-bionz/build/arm-softmmu/qemu-system-arm   # 你的 QEMU 二进制
```

## 编译探针

```bash
arm-linux-gnueabi-gcc -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp \
  -fno-builtin -DFAKE7_COUNT=0 -o libCoreSync_trace.so core-sync-trace.c
```

编译期开关：

| 宏 | 作用 |
|---|---|
| `FAKE7_COUNT=N` | 伪造前 N 次 cmd 7（`rcv_msg_tmo`）直接返回 0。实测：App 行为**无变化**（应答要写进 buf，只返 0 不够） |
| `DUMPBUF=1` | dump cmd 7 的 arg 结构在调用前后各 8 个字 |
| `DUMPQID=1` | 记录 cmd 4/5 的目标 qid（**这条最关键**，直接看出"它在给谁发消息"） |

## 三个会浪费你一整天的坑

1. **日志写 `/tmp`（ramdisk），不要写 `/setting`（FAT）**。
   否则几千次 ioctl ×2 次 write 会把 App 从 50 秒拖到 133 秒，你根本跑不到卡点。
2. **不要在探针初始化时调 `getenv`**。实测会让进程在 `exec` 后立刻进 `D`(disk sleep)、
   只剩 8 线程、239 秒加载不出目标库。要用开关就用**编译期宏**。
3. **`errno` 在成功时不会被清零**。只有 `ret == -1` 的 errno 才有意义；
   用 `ret=0` 时读到的 errno 全是上一次的残留，会得出完全错误的"错误率"。

## 许可

与仓库其余部分一致（见根目录 `LICENSE`）。

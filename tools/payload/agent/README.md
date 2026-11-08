# ZV-E10 Agent — 免 USB 的网络命令通道

一个 18 KB 的 `LD_PRELOAD` 库，注入索尼 ZV-E10 相机后提供 **TCP 7777** 命令通道，
让你可以像 SSH 一样直接跑 shell 命令，不需要插 USB、不需要服务模式。

```
$ python3 cam.py --ip 10.42.0.158 run "cat /proc/version"
Linux version 3.0.27_nl-rt106+ ...
```

---

## 它解决什么问题

ZV-E10 的官方调试入口有两个：

| 入口 | 限制 |
|---|---|
| USB 服务模式（`054c:0d95`） | 必须插线；有 30 秒空闲超时；每次都要重新认证 |
| JIRITSU 文件通道 | 需要 `/tmp/jiritsu/tmmr.jit` 轮询，延迟 1 秒，且依赖 jiritsu 进程活着 |

**TCP 7777 通道没有这两个限制**：相机连上 Wi-Fi 后，毫秒级响应，可以连续跑命令、传文件。

---

## 架构

```
/etc/ld.so.preload
  └─ /etc/zve10_agent.so          ← 本库（18 KB）
       └─ 构造函数（geteuid()==0 才动）
            └─ fork + setsid
                 └─ agent_main()
                      ├─ jiritsu_loop()   兜底通道（hex 命令）
                      ├─ watchdog()       可选：UI 没起来就回滚+重启
                      └─ start_tcp()      ★ 0.0.0.0:7777
                           └─ 每个连接 fork 一个 handler
```

**单实例保护**：用 `AF_UNIX` 抽象命名空间 socket（`@zve10_agent_v14`）做锁，
第二个实例 `bind()` 失败即退出。

---

## 协议

长度前缀，避免空格/换行被相机侧截断。

| 请求 | 响应 |
|---|---|
| `PING\n` | `PONG\n` |
| `RUN <n>\n` + n 字节命令 | `OUT <m>\n` + m 字节输出 |
| `PULL <n>\n` + n 字节路径 | `DATA <m>\n` + m 字节内容 |
| `PUSH <n>\n` + n 字节 `路径\0数据` | `OK\n` / `ERR ...` |

`RUN` 通过 `execle("/bin/sh","sh","-c",cmd,...)` 执行，输出重定向到临时文件再读回
（不依赖 pty，也不需要相机有 `head`/`tail` 这类 applet）。

`PUSH` 会自动备份原文件为 `<路径>.orig`（仅当备份不存在时）。

---

## 用法

```bash
# 检查连通
python3 cam.py --ip 10.42.0.158 ping

# 跑命令
python3 cam.py --ip 10.42.0.158 run "cat /proc/mounts"

# 拉文件
python3 cam.py --ip 10.42.0.158 pull /log/zve10_agent.log --out agent.log

# 推文件（注意：--file 是本地源，第一个参数是远程路径）
python3 cam.py --ip 10.42.0.158 push /etc/foo.so --file ./foo.so
```

> ⚠️ `PUSH` 协议有 8 KB 缓冲区上限。大于 8 KB 的文件请分块推：
> 见 `tools/payload/chunk_push.py` 的实现（base64 分块 + `busybox base64 -d` 追加）。

---

## 编译

相机是 **ARMv7 Cortex-A9 / softfp**（`Flags 0x5000002`），glibc 2.11.2。

用任意 ARMv7 hard-float 工具链也能跑（ABI 不是障碍，实测两者都能加载）：

```bash
TC=/path/to/arm-buildroot-linux-gnueabihf
$TC-gcc -shared -fPIC -O2 -o zve10_agent.so agent.c
```

**依赖只有 libc**，没有 libpthread/libdl。

检查：
```bash
readelf -dW zve10_agent.so | grep NEEDED   # 应该只有 libc.so.6
```

---

## 部署

### 前置条件

相机固件 2.02/2.03，已能通过 USB 服务模式或 JIRITSU 通道写入 `/etc`。

### 步骤

```sh
# 1. 把库放到 /etc（/etc 是 nflasha10，vfat，重启后保留）
#    用 USB 服务模式或已有的 JIRITSU 通道

# 2. 挂到 ld.so.preload
echo '/etc/zve10_agent.so' > /etc/ld.so.preload
sync

# 3. 重启相机
```

重启后，`im.elf` 启动时 `ld.so` 会加载本库，构造函数里 fork 出 agent 并监听 7777。

### 验证

```sh
cat /log/zve10_agent.log
# [21] agent v11 start uid=0
# [21] no bind mode (stock)
# [24] tcp listen 0.0.0.0:7777
```

---

## 关键坑（都是实测踩出来的）

### 1. `/etc/ld.so.preload` 对某些进程不生效

**现象**：`/etc/ld.so.preload` 写了也没用，某些进程的就是不加载。

**原因（已定位）**：这些进程在 `/etc` 挂载完成**之前**就启动了，
`ld.so` 读 `/etc/ld.so.preload` 时文件不存在。

**验证方法**：手动用相同参数启动一个实例，看它是否加载——
后来的进程一定能加载。

```sh
# 这些会加载（启动晚）
/bin/sh /usr/bin/startBTCore.sh
/usr/bin/wpa_supplicant
# 这些不会（启动早）
/usr/bin/im.elf
```

### 2. `/usr` 分区启动时被强制还原

**实测**：往 `/usr/bin/im.elf` 或 `/usr/bin/bootin.elf` 写入任何内容（甚至改成 1 字节），
**重启后都会被还原成原厂文件**（owner `57285:1000`、mtime `Jun 4 2026` 都一致）。

`/usr` 里其他文件（如新建的 `/usr/bin/_pt.txt`）**会保留**——
所以不是整个分区只读，而是**特定文件有完整性恢复**。

**结论**：`/usr` 不能用来做持久化修改，只能用 `/etc`（vfat，不校验）或运行时内存补丁。

### 3. `bootin` 的 `preload` 白名单

`bootin.elf` 在 `dmode` 含 `p`（0x40）时读 `/setting/mode/preload`，
但**内容必须以白名单前缀开头**，否则整个 `LD_PRELOAD` 被丢弃：

```
/usr/tool/LeakTracer.so        （存在）
/usr/tool/libefence.so         （不存在）
/usr/tool/libjemalloc_tsh.so   （存在）← 用这个
/usr/tool/libmcheckb.so        （不存在）
```

**格式**：`前缀库:你的库:...`，例：
```sh
printf '/usr/tool/libjemalloc_tsh.so:/etc/nm.so:/etc/zve10_agent.so' > /setting/mode/preload
printf '3p' > /setting/mode/dmode
```

### 4. 构造函数里不能用 pthread / 不能用会崩的 API

- `pthread_create` 在 `ld.so` 构造函数阶段**会死锁** → 用 `fork()+setsid()`
- `dl_iterate_phdr(NULL, NULL)`（回调传 NULL）会**段错误** →
  因为它挂在 `ld.so.preload` 里，会导致**每个进程都崩**、相机**无限重启**

**救砖只能靠 fastboot**（Pico + `stage1.bin` 从 BootROM 进）。

---

## 安全警告

> ⚠️ **这个库会让相机以 root 权限执行任意 shell 命令，且监听 `0.0.0.0`。**
>
> - 任何连上相机 Wi-Fi 的人都能完全控制相机
> - 刷入前请确认你能接受这个风险
> - 库里有一个 `watchdog()`：如果 `/log/zv1f_ui/bind.txt` 存在且 90 秒内 UI 没起来，
>   会自动撤销 bind 挂载并重启。不想要可以删掉

**救砖**：
- `/etc/ld.so.preload` 是 vfat，如果写坏了导致无限重启，
  需要 fastboot（Pico + BootROM）才能恢复
- 详见 `SAFETY.md`

---

## 文件

| 文件 | 说明 |
|---|---|
| `agent.c` | 库源码（253 行） |
| `cam.py` | TCP 客户端（106 行） |
| `README.md` | 本文档 |
| `SAFETY.md` | 刷入风险与救砖 |

## 许可

MIT

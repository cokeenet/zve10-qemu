# agent 加载链：已确认的部分与未解的部分

## 链路上的每一环

```
① /bootin.elf 读 /setting/mode/preload
② 白名单前缀校验（4 个 /usr/tool/*.so 之一）
③ putenv("LD_PRELOAD=...")
④ execve(/usr/bin/im.elf)
⑤ ld.so 加载 preload 里的库
⑥ agent 构造函数 fork → setsid → agent_main
⑦ start_tcp() bind 0.0.0.0:7777
```

## 已实测确认

| 环节 | 证据 | 结论 |
|---|---|---|
| ① preload 内容 | 直读 `/setting/mode/preload` = `/usr/tool/libefence.so:/log/agent_v14.so`（40 B，无换行） | ✅ 正确 |
| ② 白名单 | `preload_probe.txt` 显示 `exe=im.elf ppid=1 uid=0` | ✅ 通过 |
| ③④ | 同上 | ✅ |
| ⑤ | `agent_v14.so` 完整（md5 `a366a1e1…`，簇链 `[372,373]`） | ✅ 文件完好 |
| ⑥ | 日志 `[NNN] agent v11 start uid=0` 反复出现 | ✅ 构造函数执行 |
| ⑦ | 日志 `[NNN+3] tcp listen 0.0.0.0:7777` 同上 | ✅ bind/listen 成功 |

**⇒ 从 preload 到 listen，整条链是通的。**

## 未解：为什么外部连不上 7777

已知观测：

- 相机 Wi-Fi 正常（能 ping 通、能收到 ARP）。
- 相机 15740（PTP/IP）端口开放 ⇒ **网络栈和外部可达性都正常**。
- 7777 **不可达**。
- `/proc/net/dev` 在早期观测中**没有 `wlan0`**，`bcmdhd` 曾在 `Loading` 状态。

### 待验证的假设

| # | 假设 | 验证方法 |
|---|---|---|
| A | agent 的监听 socket 在 `wlan0` 出现前创建，之后未被路由 | 相机上 `cat /proc/net/tcp`，看 7777 的 local_address |
| B | 相机作 AP 时外部设备不在同网段 | 看客户端拿到的 IP 与相机 `wlan0` 是否同一网段 |
| C | 上一次 agent 进程残留，抽象 socket 锁 `zve10_agent_v14` 被占，新实例直接 return | `cat /proc/net/unix \| grep zve10` |
| D | agent 在 accept 循环里卡死 | `/proc/<pid>/wchan`、`/proc/<pid>/stack` |
| E | 相机 Wi-Fi 有防火墙（iptables）挡 7777 | `iptables -L -n` |

### 最快的判定

需要相机正常启动后，取这三项：

```sh
cat /proc/net/tcp          # 7777 是否在 LISTEN，绑在哪个地址
cat /proc/net/unix | grep zve10   # 抽象锁是否存在（假设 C）
ls /proc/*/cmdline         # agent 进程是否还活着
```

## 清理状态

`/log` 已清空到只剩 9 项（7 个原厂 + `agent_v14.so` + `zve10_agent.log`）。
`/setting` 的 6 个我们的小文件尚未清理。

# ZV-E10 Guardian — 让 im.elf 带上 LD_PRELOAD 重启

## 为什么需要这个

ZV-E10 的 `/usr/bin/im.elf`（UI 主进程）在启动时 **`/etc` 还没挂载完**，
所以 `ld.so` 读不到 `/etc/ld.so.preload`，我们的库永远进不去 UI。

实测确认：
- `im.elf` 启动于 tick 104（约 5.2 秒）
- `startBTCore.sh`（晚得多）能正常加载 preload
- **开机后手动启动的 `im.elf` 实例能加载 preload**（pid 895 实测 +4 命中）

## 做法

`/etc/guardian` 是一个守护进程，开机后（由 `/etc/ld.so.preload` 或手动触发）：

1. 读 `/etc/ld.so.preload`，取到 preload 列表
2. `putenv("LD_PRELOAD=<列表>")` 然后 `execv("/usr/bin/im.elf", argv)`
3. 如果退出，等 3 秒再起（限速）
4. 如果 `/etc/guardian.off` 存在，立刻退出

## 为什么用 putenv + execv 而不是 execve

`execve` 用自定义 `envp` 会**丢掉所有原有环境变量**。
`im.elf` 依赖 glibc 内部的一些变量，环境不全时启动会失败。

`putenv` + `execv` 保留完整环境，只额外加上 `LD_PRELOAD`。

## 风险

| 风险 | 恢复 |
|---|---|
| guardian 起不来 | 不影响系统（只是个程序） |
| 新 im.elf 崩溃 | guardian 会重试，最多 10 次后放弃 |
| UI 黑屏 | `touch /etc/guardian.off` 禁用，或重启 |
| 无限重启 | 有 3 秒限速 + 10 次上限 |

**`/etc/guardian.off` 是安全开关，任何时候都能用 `touch` 创建。**

## 安装

```sh
# 1. 推二进制到 /etc/guardian（vfat，持久）
# 2. 在相机上执行
sh /etc/install.sh
```

## 禁用

```sh
touch /etc/guardian.off
```

## 日志

```sh
cat /log/guardian.log
```

## 关键前提

`/etc/ld.so.preload` 里必须有我们的库。当前配置：
```
/etc/zve10_agent.so
/etc/libnewmenu.so
```

注意：`libnewmenu.so` 必须能被 `im.elf` 正常加载，
否则 `im.elf` 会崩，guardian 会不断重试直到 10 次上限放弃。

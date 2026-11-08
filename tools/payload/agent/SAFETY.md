# 安全与救砖

## 风险等级

**刷入本库 = 给相机 root 后门 + 网络 shell。**

| 风险 | 后果 |
|---|---|
| 库本身有 bug | 挂到 `ld.so.preload` 后**每个进程**都会加载它，构造函数崩 = **每次开机都崩 = 无限重启** |
| 网络暴露 | `0.0.0.0:7777` 无认证，同一网络任何设备可执行任意命令 |
| 写坏 `/etc/ld.so.preload` | 同上，无限重启 |

---

## 已知会变砖的写法

### ❌ 在构造函数里调用会崩的 API

```c
// 这行代码让相机无限重启了
dl_iterate_phdr(NULL, NULL);   // 回调传 NULL → 段错误
```

因为库挂在 `ld.so.preload`，**每个进程**（`init`、`bootin`、`im.elf`、`sh`…）
加载它时都会崩，系统根本起不来。

**教训**：构造函数里只做最小、最安全的事；所有返回值都要检查。

### ❌ 在构造函数里用 pthread

```c
pthread_create(&t, NULL, worker, NULL);   // ld.so 阶段会死锁
```

`ld.so` 在构造函数执行时还没准备好 pthread 的 TLS，
`pthread_create` 会死锁，进程卡死。

**替代**：`fork() + setsid()`，或者用 `setitimer` + 信号。

### ❌ 假设 `/usr` 可以持久修改

`/usr/bin/im.elf`、`/usr/bin/bootin.elf` **重启后会被还原**。
往那里写的任何东西都不会生效。

---

## 救砖

### 情况 A：还能进系统（Wi-Fi / USB 可用）

```sh
# 清空 ld.so.preload
echo '' > /etc/ld.so.preload
sync
reboot
```

### 情况 B：无限重启，Wi-Fi 起不来

用 **Raspberry Pi Pico + BootROM 注入**（需要硬件）：

1. Pico 刷入 `pico_sender` 固件
2. 跑 `inject_payload.py --file stage1.bin --duration 180`
3. 相机插 USB → 自动进 fastboot
4. 加载 `stage2-flash.bin` → 拿到 eMMC 读写
5. 直接改 `nflasha10`（`/etc` 分区）的 `ld.so.preload`

详见 `fastboot/` 目录。

### 情况 C：连 fastboot 也进不去

**只能拆机接 eMMC**，或者送修。

---

## 部署前的检查清单

- [ ] 确认能通过 fastboot 救砖（Pico 在手、`stage1.bin` 可用）
- [ ] 确认 `/etc/ld.so.preload` 内容正确（只有你想要的库）
- [ ] 库已经在本机验证过：`readelf -d` 只有 libc 依赖
- [ ] 构造函数里没有 pthread / 没有 `dl_iterate_phdr(NULL,...)`
- [ ] 第一次部署前，先在 `/etc` 里放一个**最小的**测试库（只写日志），
      确认能加载、相机能正常启动，再换完整版

---

## 最小测试库模板

```c
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

__attribute__((constructor)) static void boot(void) {
    int fd = open("/log/test.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (fd >= 0) { 
        const char *m = "loaded\n";
        write(fd, m, strlen(m));
        close(fd);
    }
}
```

先验证这个能工作，再加功能。

---

## 一个真实的事故记录

2026-11，我们往 `/etc/ld.so.preload` 里推了一个测试库，里面有：

```c
dl_iterate_phdr(NULL, NULL);
```

结果相机**无限重启**，Wi-Fi 完全起不来，只能靠 Pico + fastboot 从 `nflasha10`
分区把 `ld.so.preload` 改回来。

**这个库从写好到变砖只用了 30 秒。**

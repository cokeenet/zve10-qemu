# 为什么每次重跑 bootin 相机都会断网几分钟

## 一、观察到的现象（两次重复）

```text
第一次（23:33）：kill bootin(149) + 手动跑 bootin → 相机从 AP 断开，约 10 分钟后回来
第二次（23:58）：同样操作 → 相机又断开
```

## 二、根因分析

### 原因：bootin 负责启动 Wi-Fi 相关子系统

从 bootin 的 .rodata 和代码看：

```text
· bootin 是启动链的核心：解析 dmode → 设置环境 → fork/exec im.elf
· 它还管理：
    - qsi.elf（Sony 的 QoS/系统接口）："/usr/bin/qsi.elf &" 和 "/usr/bin/qsi.elf noevent0 &"
    - gdbserver（可选）
    - 各种 mount（/dev/pts 等）
    - 通过 /proc/mounts 检查挂载状态
· **杀掉它 = 杀掉这些子系统的父进程 = 相关服务可能被连带重启**

具体到网络：
· pump（pid 762/761）是 Sony 的网络管理进程
· wpa_supplicant（pid 750/752）是 Wi-Fi 关联
· 二者可能都依赖 bootin 启动时建立的环境（或者它们的父进程是 bootin 的子进程）
```

### 更可能的具体原因

```text
bootin 是 pid 149，**ppid = 1**（launch_shell）。
但它 fork 出来的一堆子进程（包括可能启动 wpa_supplicant/pump 的脚本）
在 bootin 被杀后成为孤儿，被 init(1) 收养。

如果 bootin 用了 setsid/进程组，杀掉它可能导致整个进程组被终止
⇒ wpa_supplicant 和 pump 一起死
⇒ Wi-Fi 掉线
```

## 三、为什么相机最终能自己回来

```text
· launch_shell(1) 的 shell_loop 会持续 fork/exec
· 或者某个 watchdog 重新拉起了 Wi-Fi 相关服务
· 从 23:33 断开到 23:44 回来 = 约 11 分钟
  这个时间很像 Sony 的某个看门狗超时周期
```

## 四、教训与对策

```text
✗ 不能杀 bootin —— 它是启动链和多个子系统的父进程
✓ 只能杀 im.elf，然后想办法让 bootin 重新 fork 一个 im.elf

那 bootin 有没有提供"重拉 im.elf"的接口？
从字符串看：
  "Bb fork_imelf()"     ← 日志字符串，说明有 fork_imelf 函数
  "Bb cmdline"          ← 打印 im.elf 的命令行
  "Bb clearing env"     ← 清环境
  "killall : kill existing bootin.elf"  ← 只杀 bootin 自己

⇒ 需要看 bootin 是否有 signal handler 或者等待 im.elf 退出的逻辑。
  如果有，那么杀掉 im.elf 后 bootin 会自己重拉。
  **实测：杀掉 im.elf 后 20 秒内没有重拉** ⇒ 说明没有。
```

## 五、替代方案（不杀 bootin）

```text
方案 1：写一个"第二个 im.elf"，用 LD_PRELOAD 带我们的库
  · 不改 bootin，不杀任何进程
  · 直接在 ash 里：LD_PRELOAD=/log/menu_ui.so /usr/bin/im.elf debug s g r u l i a lazy-global &
  · 但会有两个 im.elf 同时跑 ⇒ 可能冲突（第二个可能抢不到 UI 资源）
  · 风险：中

方案 2：等相机自己重启（WBI 温启动），但那不会重读 dmode

方案 3：用 /etc/ld.so.preload 加载（但 im.elf 启动时 /etc 还没挂 ⇒ 无效，已验证）

方案 4：写一个小的 stub 替换 /usr/bin/im.elf？（/usr 是 ro，不行）

方案 5：★ 最可行：直接启动 im.elf 并显式带 LD_PRELOAD
  在 ash 里：
    cd /usr/share/app
    LD_PRELOAD=/log/menu_ui.so /usr/bin/im.elf debug s g r u l i a lazy-global > /log/im2.log 2>&1 &
  · 完全不碰 bootin、不碰 /setting
  · 若第二个 im.elf 能起来，我们的库就在它里面了
  · 若冲突，它自己会退出，不影响相机
```

## 六、下一步

```text
★ 改用方案 5：不杀 bootin、不改 dmode/preload，直接带 LD_PRELOAD 启动 im.elf
  这是最干净的方式：
   · 零持久化改动（env 随进程消失）
   · 不碰 bootin
   · 不碰 /setting
   · 若失败，什么都不用回滚

需要的环境（已备份在 im.environ）：
  HOME=/  TERM=linux  PATH=/sbin:/bin:/usr/sbin:/usr/bin  TOPRC=/etc/toprc
  MALLOC_OPTIONS=gfffffffff
  MALLOC_MMAP_THRESHOLD_=32768  MALLOC_TRIM_THRESHOLD_=32768  LT_ABORTREASON=0
  cwd = /usr/share/app
```

---

## 七、实测确认：两次都是杀 bootin 导致断网

```text
第 1 次 23:33 → 23:44 回来（约 11 分钟）
第 2 次 23:58 → 至今（>8 分钟未回）
⇒ **两次都发生在 kill bootin(149) 之后**
⇒ 因果关系明确
```

## 八、已修改的菜单库（v3）

上一版 `menu_ui.so` 有三个致命问题，v3 全部修正：

| 问题 | v1（失败版） | v3（当前） |
|---|---|---|
| 构造函数阻塞 | `sleep(10)` ⇒ 卡住 im.elf 启动 ⇒ watchdog 杀 | **`pthread_create` + `pthread_detach`，立刻返回** |
| 找不到符号 | `dlsym(RTLD_DEFAULT)` ⇒ 返回 NULL（libObj 是 RTLD_LOCAL） | **`dlopen()` 拿 handle，再用 `dlsym(handle)`** |
| 跨进程无效 | `fork()` ⇒ 子进程改的是地址空间副本 ⇒ 画不到屏幕 | **`pthread`（同地址空间）** |

**验证（`zprobe.elf` 实测）：**
```text
dlsym(handle) 6/6 全部找到符号：
  sys_getInstance = 0x5450fe5
  genID           = 0x5476bbd
  createLayer     = 0x54511fd
  Layer::setActive= 0x544f3e3
  GEN_Panel       = 0x4f9e451
  GEN_Label       = 0x4f4fbb1
推导 base = 0x4e3a000（页面对齐 ✓）
```

**但同一进程里 `sys_getInstance()` 返回 NULL**
```text
⇒ 因为那个进程是新 dlopen 的 libObj，单例从没被初始化
⇒ **必须在 im.elf 里面**（那里单例已初始化）
⇒ v3 用 pthread（同进程）是对的
```

## 九、新的安全启动方式（`launch_direct.sh`）

```sh
export HOME=/ TERM=linux PATH=... TOPRC=/etc/toprc
export MALLOC_OPTIONS=gfffffffff MALLOC_MMAP_THRESHOLD_=32768 MALLOC_TRIM_THRESHOLD_=32768
export LD_PRELOAD=/log/menu_ui.so      # ← 只在这次进程里生效
cd /usr/share/app
/usr/bin/im.elf debug s g r u l i a lazy-global &
```

**关键优点：**
```text
✓ 不杀 bootin（不会断网）
✓ 不改 dmode / preload（零持久化改动）
✓ LD_PRELOAD 只在这次进程生效，进程退出即消失
✓ 若第二个 im.elf 起不来，什么都不用回滚，相机照常
✗ 唯一风险：两个 im.elf 同时跑可能抢 UI 资源
```

**→ 所以更稳的做法还是先杀掉原来的 im.elf，但**保留 bootin**：
```sh
kill im.elf  →  sleep 2  →  LD_PRELOAD=... im.elf ... &
```
这样 bootin 一直在，Wi-Fi 不会断，而新的 im.elf 带着我们的库。

# ★ WBI 温启动失败根因（本轮决定性发现）

## 一、内核日志给出的直接原因

```text
[ 1148.780236] Stopping user tasks ... (elapsed 0.02 seconds) done.
[ 1150.085255] ssboot: optimizing image...
[ 1150.088961] ssboot: cannot open file: /log/ssbi_prof.bin     ← ★★★ 关键
[ 1150.089068] ssboot: failed ssboot_basic_read_profile() -2
[ 1150.089160] ssboot: failed to optimize image: -2
[ 1150.089271] Restarting tasks ... done.
```

**⇒ 快照优化阶段因为 `/log/ssbi_prof.bin` 不存在而失败（返回 -ENOENT）。**

## 二、这意味着什么

```text
· 相机当前 state = snapshot
· image_dev = *wbi4, image_mode = 2
· 但优化失败 ⇒ 快照**没有按预期的优化版本生成**
⇒ 之后 drop 快照再冷启动，就等于「用一个半成品快照去开机」
⇒ 这正是你遇到「一 image_drop 就砖」的机制
```

## 三、`ssbi_prof.bin` 是什么、谁该写它

```text
· 名字 ssbi = snap shot boot image profile
· 位置 /log（nflasha11，500MB，vfat）
· 由 ssboot 子系统在「优化镜像」阶段读取
· **我在固件树里搜 'ssbi' 只找到 bsa_server 的一个同名选项，找不到生成者**
  ⇒ 它可能是出厂时就写好、或由某个我看不到的组件写
· **当前 /log 里完全没有这个文件**

## 四、需要回答的关键问题

```text
Q1: 这个文件是不是本来就不存在（这台机器从没生成过）？
Q2: 还是被我（或之前的操作）删掉了 / /log 曾经写满导致丢失？
Q3: 它是否可以从别处复制/重建？
Q4: 若无法恢复，是否可用「不 drop、只清 snapshot 标记」的方式让 bootin 重跑？
```

## 五、由于这个问题，必须先放弃 drop 路线

```text
用户明确说「我们现在一 image_drop 就砖」
⇒ 与内核日志完全吻合：优化失败 ⇒ 快照是残缺的 ⇒ drop 后冷启动必失败
⇒ **绝对不能 drop**
```

## 六、替代路线（本轮要验证）

既然不能 drop，就必须找到「不 drop 也能让 bootin 重新执行」的办法。

**已知事实：**
```text
· launch_shell.elf (pid 1) 有 shell_loop：fork → execv → wait4 → sleep(2) → 循环
  （反汇编确认：0x899e 循环头，0x8a08 execv，0x8a1a wait4，0x8a20 sleep 2，0x8a24 跳回）
· **但它 execv 的是 /bin/ash --login，不是 bootin**
· bootin(149) 与 im.elf(157) 的 ppid 都是 1（被 launch_shell 的子进程 exec 后接管）
· im.elf 的 cwd = /usr/share/app
· im.elf 的 environ 已记录（HOME=/ TERM=linux PATH=... MALLOC_OPTIONS=gfffffffff 等）
```

**⇒ 候选做法（下一步实测）：**
```text
做法 A: 手动复刻 bootin 的 im.elf 启动参数，但**先把 LD_PRELOAD 塞进环境**
        （完全绕开 dmode/preload 文件，直接 setenv + exec）
        · 风险：不经过 bootin 的初始化，可能 UI 起不来
做法 B: 让 launch_shell 重跑 bootin（用它 --login 的 shell 里 exec bootin）
        · 风险：bootin 可能因已有实例而拒绝（它有 killall 选项）
做法 C: 找到 launch_shell 的 shell_loop 到底 exec 什么（可能读某个配置）
```

**优先做 A**，因为它最可控：不碰分区、不碰 WBI、只在环境变量里加一项。

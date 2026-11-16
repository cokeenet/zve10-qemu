# ★ 上游 A6400-Extra-Menu 完全没碰 WBI（已全量确认）

## 一、全量搜索结果：零命中

我在归档的 77 个源文件里全量搜索：

```text
wbi / WBI          → 0 处（只有 film_settings_snapshot 里 "snapshot" 是指设置快照，不是 WBI）
image_drop         → 0 处
image_mode         → 0 处
ssboot             → 0 处
warm_boot / warm.mode → 0 处
/sys/power/...     → 0 处
```

**⇒ 上游项目**完全没有**清 WBI 的代码、脚本或文档，
   连提都没提这件事。**

## 二、那它怎么做到"开机自动加载"？

它用的是**完全不同的机制**：SD 卡 + 冷启动构造器。

### 2.1 三层结构（`host-tests/film-runtime-v2-templates/`）

```text
bootstrap-v2.sh     ← "Cold-boot constructor only"
  ├─ 检查 killswitch（/tmp/disable_hook、/mnt/disable_hook、/mnt/A6400/DISABLE…）
  ├─ **early_boot()：确认自己在启动 30 秒内**
  ├─ mount -t vfat -o ro /dev/mmca1 /mnt      ← 只读挂 SD 卡
  ├─ 校验 /mnt/A6400/RUNTIME/prepare.sh 的 sha256
  ├─ exec /bin/sh prepare.sh
  │
prepare.sh          ← 只拷贝、不执行
  ├─ 逐项校验 sha256
  ├─ 把件拷到 /tmp_bt（RAM）
  └─ 写 receipt
  │
runtime-start.sh    ← "RAM-only native-loader entry"
  ├─ gate()：检查 /proc/mounts 里没有 /dev/mmca*（SD 必须已卸载）
  ├─ ready()：确认 /proc/149/comm == im.elf，且 maps 里有：
  │            /usr/bin/im.elf
  │            /usr/lib/libObj.so
  │            /usr/lib/viewUnified2.so
  ├─ 准备 a6400-ui-load.req（请求头）
  └─ 执行 a6400-ui-load-guard.elf   ← **这就是注入器**
```

### 2.2 ★ 关键发现：`early_boot()` 判断"启动 30 秒内"

```sh
early_boot() {
    test -e "$AELOCK" && test ! -L "$AELOCK" || return 1
    read -r uptime rest < "$UPTIME" || return 1
    seconds=${uptime%%.*}
    test "$seconds" -le 30 || return 1        # ← **必须开机 30 秒内**
    ...
}
```

**⇒ 它**假设自己的脚本是在冷启动早期被执行的**（由某个 boot hook 拉起），
   而不是在相机运行中被手动调用。
   （`bootstrap-v2.sh` 第一行注释就写着 "Cold-boot constructor only. Never invoke
   this from a running shooting camera."）

### 2.3 它靠什么被拉起？（仓库里没直接给）

```text
· README 说「新版整合程式冷開機自動載入」→ **尚未完成驗收**
· 功能狀態.md 同样写「尚未完成新版整合程式的 SD 冷開機驗收」
· 仓库里**没有**提供把 bootstrap-v2.sh 挂到启动链的代码
⇒ 说明这部分要么在私有版本里，要么还没完成
```

**⇒ 更可能：它依赖 A6400 上**本来就存在**的某个启动机制
   （比如 DevCrashDay/a6400 那个项目的 LD_PRELOAD hook，
     用 `libAK8975.so` 的 DT_INIT 构造函数拉起 autoexec.sh）。

### 2.4 它用 ptrace 注入（不是 LD_PRELOAD）

```text
runtime-start.sh 调用 a6400-ui-load-guard.elf
⇒ 那个 guard 就是 `native_ui_load_guard.c`：**ptrace 冻结 im.elf 的线程，
   往 /proc/149/mem 写 payload**
⇒ 它申请是「运行中注入」，不需要重启 im.elf、不需要改 dmode/preload
```

**⇒ 这解释了为什么它不需要碰 WBI。**

## 三、★ 它与我们的关键差异（这才是重点）

| 维度 | A6400-Extra-Menu | 我们（ZV-E10） |
|---|---|---|
| 注入方式 | **ptrace 冻结 149 的线程 + 写 /proc/149/mem** | LD_PRELOAD（需要重启 im.elf）|
| 需要冷启动 | **不需要**（运行中注入） | 需要（除非用 ptrace）|
| 碰 WBI | **不碰** | 之前想清 |
| 断网风险 | **无**（不杀任何进程） | 有（杀 im.elf 断网）|

**⇒ 上游的做法恰恰绕开了我们遇到的全部问题！**

## 四、但我们试过 ptrace，会让相机重启

```text
我用 freeze_test.elf / tids_test.elf 对 im.elf(157) 的 201 个线程做 ATTACH，
结果相机重启了（uptime 从 157s 掉到 130s）

⇒ 但 A6400 的 guard 是对 **pid 149**（bootin，不是 im.elf）
   而且 A6400 的 im.elf 是 pid 149 的子进程
   我们 ZV-E10 上：bootin=149、im.elf=157
   ⇒ **A6400 的 149 是 bootin，我们的 149 也是 bootin！编号巧合一致**
```

**⇒ 所以 A6400 注入的时候冻的是 bootin(149) 的线程，
   不是 im.elf(157)。而我之前冻结的是 im.elf(157) ⇒ 冻错了 ⇒ 重启。**

**★ 这是一个关键的重新理解：A6400 的代码里 `maps_ok()` 检查的是
   `/proc/149/maps` 里的 `/libObj.so` —— 说明 A6400 上 **pid 149 就是 im.elf**，
   不是 bootin。而我们的 149 是 bootin，im.elf 是 157。**

**⇒ 这就解释了为什么我们的 ptrace 会失败：我们冻的是 157（im.elf），
   而 A6400 冻的是它自己的 149（也是 im.elf）—— 但两边**冻结目标性质相同**，
   理论上应该一样…除非我们的 im.elf 对 STOP 更敏感（RT 内核 + watchdog）。**

## 五、结论：上游没有清 WBI，它用 ptrace 运行中注入

```text
★ 回答你的问题：**A6400-Extra-Menu 没有任何清 WBI 的代码**。
★ 它靠 ptrace 往 im.elf 写 payload（运行中注入），完全不碰启动链、
  不碰 dmode/preload、不碰 WBI、不需要冷启动。
★ 它唯一要求的是"启动 30 秒内的早期执行"来拉起 bootstrap；
  这部分仓库里没提供，README 也标为"尚未验收"。
```

**⇒ 对我们的启示：**
```text
1. 清 WBI 这条路从来没被上游验证过 —— 所以风险确实高
2. 上游的方案压根不需要冷启动/清 WBI，它就是运行中 ptrace 注入
3. 我们应该重新审视 ptrace 路线：
   · 之前失败可能不是因为"ptrace 不行"
   · 而是因为我的 test 对 **201 个线程全部 ATTACH 后没有立即 DETACH**
   · 或者 suspend 时间太长触发 RT watchdog
```

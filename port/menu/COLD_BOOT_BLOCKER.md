# ★★★ 核心阻碍确认：连拔电池都走 WBI 温启动

## 一、决定性证据（本次拔电池后）

```text
/proc/149/stat（bootin）starttime = 97  jiffies
/proc/157/stat（im.elf）starttime = 0   jiffies   ← 比 bootin 还早
/proc/uptime = 169 秒
```

**im.elf 的 starttime 比 bootin 还早 ⇒ 它不可能是 bootin fork 出来的，
只可能是内核**从 WBI 快照里直接恢复**的。**
（starttime=0 表示"进程存在于内核启动的最早期"）

## 二、结论

```text
★ **拔电池 ≠ 冷启动**
★ 只要 WBI 快照有效，内核就会走温启动，从快照恢复所有进程
★ 温启动 ⇒ bootin **不执行** ⇒ 不读 /setting/mode/dmode 和 preload
★ 所以：改 dmode/preload 对开机路径完全无效（已验证 3 次）
```

## 三、这解释了之前所有失败

```text
第 1 次（round 3）：改 dmode=3p + preload → reboot → 库没注入    ← 温启动跳过
第 2 次（本次）：改 dmode=3p + preload → 拔电池    → 库没注入    ← 温启动跳过
```

## 四、唯一能拿到"库进 im.elf"的方式（已成功过 1 次）

```text
手动重跑 bootin：
  1. kill -15 im.elf
  2. kill -15 bootin
  3. cd /usr/share/app && /usr/bin/bootin.elf &
  ⇒ bootin 真正执行 ⇒ 读 dmode=3p + preload ⇒ putenv LD_PRELOAD ⇒ fork im.elf
  ⇒ **库进 im.elf**（证据：nm-probe3.log = "P3 pid=355 tid=355 comm=im.elf"）
```

**代价：** 相机网络中断约 4~5 分钟（之后自己恢复，或需拔电池）。

## 五、v4 已经证明「库能进 im.elf」，只是时机太早

```text
v4 的日志（那次实验留下）：
  === V4 WORKER tid=2148 stage=1 ===
  libObj base (from maps, offset 0, r-x-p) = (nil)
  
⇒ **V4 WORKER 出现了 ⇒ 库确实在 im.elf 里跑了**
⇒ 但构造函数执行时 libObj 还没被 im.elf dlopen 进来（懒加载）
⇒ 所以找不到符号
```

**v4+ 已修正：轮询等待 libObj 出现（最多 90 秒）。**

## 六、当前可选的下一步

### 方案 1（推荐）：手动重跑 bootin + v4+ 轮询版

```text
代价：一次 4~5 分钟网络中断
收益：能确认 stage1 是否拿到真单例
如果成功 ⇒ 可以进 stage2（createLayer）⇒ 离菜单显示很近了
```

### 方案 2：清 WBI 拿真正的冷启动

```text
代价：可能砖（但有完整备份：/log 60MB + SD 卡 60MB + 固件包出厂 34MB）
收益：冷启动后 bootin 会真的执行 ⇒ dmode/preload 生效
```

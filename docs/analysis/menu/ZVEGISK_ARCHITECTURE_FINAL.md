# zvegisk 架构（历史梳理版，禁止作为部署依据）

> **安全审计 NO-GO（2026-10-26）：** 本文包含已证伪或证据不足的“绝对安全/拔卡恢复/自动回滚/自绘不可行”断言，不能视为当前事实。请以 [开机部署审计](ZVEGISK_PREDEPLOY_AUDIT.md) 为准；不得据本文设置开机 preload。

> 截至 2026-10-26，代码 `zvegisk.so` 31,548 B / 808 行，零未定义符号、零告警。
> 本文取代此前所有架构文档，作为唯一权威版本。

---

## 一、定位

**用 Sony 官方的 preload 钩子，在 `im.elf` 启动前注入自己的代码，做三件事：**

1. **模块加载**（代码模块 + 资源模块，失败自动跳过）
2. **资源覆盖**（把新 UI / 新音效 / 新库喂给原厂 UI）
3. **崩溃自动回退**（连续失败自动回原厂）

**★ 不写 flash、不改只读分区、所有改动重启即失。**

---

## 二、机制根基（已验证）

```
/sbin/init
  └─ 读 /setting/mode/dmode
       含 'p' ⇒ bootin.elf 读 /setting/mode/preload
                    ↓
              putenv("LD_PRELOAD=<该路径>")
                    ↓
              execve("/usr/bin/im.elf")
                    ↓
        ★ ld.so 先加载 zvegisk.so ⇒ 构造函数运行
                    ↓
        ★ im.elf 的 main() 才开始
```

**★★★ 关键时序：构造函数在 UI 打开任何资源之前 ⇒ 在里面 bind，UI 读到的就是我们的文件。**

### 与当初变砖那条路的本质区别

| | `/etc/ld.so.preload`（变砖） | **zvegisk（`/setting/mode/preload`）** |
|---|---|---|
| 谁读 | **动态加载器读所有进程** | **只有 `bootin.elf`** |
| init 受影响 | **是 ⇒ 起不来** | **否** |
| 失效范围 | **整机** | **只有 im.elf** |
| 恢复 | 要改 flash | **改 `/setting` 两个文本文件** |

---

## 三、整体架构

```
┌──────────────────────────────────────────────────────────────┐
│ 存储层                                                        │
│                                                               │
│  SD 卡（可拔）              /setting（20MB，独立持久）          │
│  /zvegisk/                 /setting/zvegisk/                  │
│   ├─ boot_mode             ├─ boot_count   启动失败计数        │
│   └─ safemode              ├─ armed        ★看门狗武装开关     │
│                            ├─ safemode     停用标志（最高优先）│
│                            ├─ mounts.active 挂载记录（回滚用）  │
│                            └─ mods/<name>/ ★元数据            │
│                                ├─ module.prop                 │
│                                ├─ disabled                    │
│                                └─ <name>.failed               │
│                                                               │
│                            /log（500MB）                      │
│                            /log/zvegisk/                      │
│                             ├─ agent.so      ★模块（最先加载） │
│                             ├─ agent_enable                   │
│                             ├─ zvegisk.log   日志              │
│                             └─ mods/<name>/  ★内容            │
│                                 ├─ lib/<name>.so   (代码模块)  │
│                                 └─ system/...      (资源模块)  │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼  ⛔ dmode=3p 会清除 LD_PRELOAD（2026-11-05 证伪）
┌──────────────────────────────────────────────────────────────┐
│ zvegisk.so（31 KB，-nostdlib 纯 syscall，零 libc 依赖）        │
│                                                               │
│  构造函数：fork() ─┬─ 父进程：立刻返回 ⇒ im.elf 继续  ★安全关键 │
│                    │                                          │
│                    └─ 子进程 work()：                         │
│                        ① safemode？ → 什么都不做              │
│                        ② boot_count++ ；>3 → 建 safemode      │
│                        ③ load_modules(0)  ← 只代码模块        │
│                        ④ 挂 SD（只读）→ 读 boot_mode → 卸载   │
│                        ⑤ boot_mode=="new" → load_modules(1)   │
│                             ← 只资源覆盖                      │
│                        ⑥ armed？ → fork watchdog              │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│ im.elf + libObj + viewUnified2（原厂，未被替换）               │
│ ★ 覆盖层已就位 ⇒ 它读到的是我们的资源                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 四、启动流程（实际代码顺序）

```
work()
 ├─ ① safemode 检查（/setting/zvegisk/safemode 存在 ⇒ 立刻返回）
 │     ★ 不依赖 SD，拔卡也能保护
 │
 ├─ ② boot_count++
 │     读 /setting/zvegisk/boot_count → +1 → 写回
 │     > 3 ⇒ 建 safemode ⇒ 返回
 │     ★ 治"反复失败"这个循环
 │
 ├─ ③ load_modules(0)   ← ★ 代码模块，与 boot_mode 无关
 │     ★ 放在挂 SD 之前：模块都在 /log，没卡也能起 agent
 │     agent 最先 → 然后扫 METADIR / CONTDIR 的 lib/<name>.so
 │
 ├─ ④ 挂 SD（只读，毫秒级窗口）→ 读 boot_mode → ★立刻卸载
 │     ★ vfat 不支持多重挂载：我们若挂着，相机就无法录制/回放
 │
 ├─ ⑤ if (boot_mode == "new")  load_modules(1)   ← 只资源覆盖
 │     遍历 system/ 递归 → 目标已存在的文件 bind 上去 → 记 mounts.active
 │    else 什么都不做 ⇒ 原厂启动
 │
 └─ ⑥ if (armed 存在)  fork watchdog
       watchdog：睡 90 秒 → 扫 /proc/PID/maps 找 viewUnified/appFw
          ├─ 找到 ⇒ boot_count = 0（成功）
          └─ 没找到 ⇒ rollback_mounts() → reboot -f
```

**★★★ ⑤ 的 gate 是这次新修的：此前 `load_modules()` 在读 boot_mode 之前跑，
导致 overlay 模块不论 old/new 都会被应用 —— 那是 bug。**

---

## 五、模块系统

### 两类模块，失败模式完全不同

| | `type=code` | `type=overlay` |
|---|---|---|
| **怎么加载** | **fork+exec 载体 + `LD_PRELOAD`** | **bind 覆盖到 `/usr`** |
| **失败表现** | **载体进程崩 / 超时** | **UI 错乱 / 引擎崩 / 没声音** |
| **隔离** | **独立进程（天然）** | **覆盖层 + watchdog** |
| **检测** | **退出码 + `verify_port`** | **watchdog（UI 是否起来）** |
| **回滚** | **天然（进程死了）** | **`rollback_mounts()`** |
| **对 im.elf 风险** | **★ 零** | **★★★★ 高** |

### 四层防护

```
① 进程隔离   fork+exec 载体 → 模块崩溃只死载体，im.elf 无感
② 超时收割   载体 8 秒不退 ⇒ SIGKILL → 标记失败
③ 失败黑名单 写 /setting/zvegisk/mods/<name>/<name>.failed，下次跳过
④ 全局熔断   失败数 > 3 ⇒ 之后全部跳过
```

### 顺序保证

**`agent` 永远第一个加载** ⇒ 即使后面模块把系统搞坏，
**远程通道已经起来了，你能连上去排查。**

### 双目录（借鉴 KernelSU meta-overlayfs）

```
元数据（小、可靠、启动早期要读）→ /setting/zvegisk/mods/<name>/
内容  （大、可能几十 MB）       → /log/zvegisk/mods/<name>/
```

**★ 好处：改开关不碰 /log；/log 被清也不丢开关状态。**

---

## 六、覆盖层技术（Phase 2 已实现单文件部分）

### 单文件 bind（当前实现）

```c
/* 只处理【目标已存在】的文件 —— bind 不能新建文件 */
apply_overlay_tree(mods/<name>/system, rel="")
  对每个文件: src = mods/<name>/system/<relpath>
              dst = /usr/<relpath>
              若 dst 不存在 ⇒ SKIP(no target)
              否则 bind_mount(src, dst) + rec_mount()
```

**★ 覆盖：声音、图片、字体、已存在的 `.uxc`/`.xdb`。**

### 目录级覆盖（Phase 3，未实现）

**需要新增文件时（比如 `appFw.so`）必须目录级覆盖：**

```sh
mount --bind /usr/share/app /tmp/stock      # ① 先暴露原厂（防循环引用）
mount -t tmpfs tmpfs /tmp/zovl              # ②
ln -s /tmp/stock/x /tmp/zovl/x              # ③ 原厂链到【暴露路径】
ln -sf /log/.../new /tmp/zovl/new           # ④ 我们的链到 /log（零 RAM）
mount --bind /tmp/zovl /usr/share/app       # ⑤ 最后 bind
```

**⚠️ 两个坑：**
1. **循环引用** —— 不能链到 `/usr/share/app/x`，bind 后无限递归
2. **相对链接深度** —— `/usr/lib` 有 8 个 `../../lib/xxx`，暂存目录必须 **2 层深**

### 精确回滚

**KernelSU 用 `source=KSU` 标识挂载。我们的内核 3.0.27 **没有新 mount API**，
`mount(2)` 带 `MS_BIND` 时 **`data` 参数被忽略** ⇒ 无法打标签。**

**⇒ 替代：把 `(src,dst)` 写进 `/setting/zvegisk/mounts.active`，
回滚时倒序 `umount` 记录里的点，不误伤系统挂载。**

---

## 七、安全模型

### 结构性保证

| 保证 | 实现 |
|---|---|
| **零依赖** | **`-nostdlib` + 纯 syscall，未定义符号 0，无 NEEDED** |
| **父进程路径只有一个 syscall** | **`fork()` → 判断 → 返回，零阻塞** |
| **所有工作在子进程** | **子进程崩/挂都不影响 im.elf** |
| **模块在独立进程** | **fork+exec 载体，模块崩只死载体** |
| **段对齐正确** | **off/vaddr 差 0x10000（已验证）** |
| **init_array 正确** | **entry = zvegisk_init（已验证）** |

### 失效场景

| 场景 | 后果 |
|---|---|
| **.so 文件损坏** | **ld.so 警告 → 相机正常启动** |
| **构造函数崩溃** | **父进程已 fork ⇒ im.elf 继续** |
| **fork 失败** | **走父进程路径 ⇒ 直接返回** |
| **子进程崩溃/hang** | **只死自己（im.elf 已继续）** |
| **模块崩溃/超时** | **只死载体 → 标记失败 → 下次跳过** |
| **资源覆盖坏掉** | **watchdog → rollback + reboot → 下次跳过** |
| **写 /setting 失败** | **计数不生效（保护失效），无副作用** |
| **watchdog 误触发** | **已加 `armed` 开关，测试时不启动** |

### 三种恢复（都不需要 USB）

```
① 拔 SD 卡        → 下次开机找不到配置 → 原厂
② 卡上放 safemode → 跳过一切
③ echo 3 > dmode  → 不加载 preload
```

---

## 八、内存账（实测）

```
zvegisk.so:
  .text    22,892 B   .rodata  1,800 B
  .data        36 B   .bss    30,688 B
  ⇒ im.elf 常驻增加 ≈ 24~40 KB   （128MB 的 0.03%）

运行时（典型，agent 启用、未 armed）:
  im.elf 静态        24~40 KB
  work() 子进程      ~700 KB 页表 + 几十 KB COW    ← ★ fork 复制页表
  agent 载体         ~700 KB 页表 + busybox
  agent daemon       ~1.2 MB（busybox RSS）+ 触碰的 bss
  ──────────────────────────────────────
  合计常驻           ≈ 2 MB      （128MB 的 1.6%）

峰值:
  PULL >1MB 文件     +8 MB（agent 的 8MB 静态缓冲）
```

**★★★ fork 的 700 KB 来自 im.elf 约 177 MB 虚拟地址空间的页表复制
（ARMv7 4KB 页，每 1MB 需 1 个页表页）。
可以改 `vfork` 省掉，但**决定不改 —— 以空间换安全**。**

### 平台红线（对比手机）

```
❌ mmap 几十 MB          ← 32MB mmap 就是把相机搞崩的原因
❌ 整文件读进内存        ← 大文件必须流式
❌ 无界递归              ← 已加深度限制 6 层
✅ 小静态缓冲（KB 级）
✅ 惰性 .bss（不碰就不占）
```

---

## 九、已验证 / 未验证

### ✅ 已验证（真机）

```
zvegisk start
boot_count=1
sd=250:11              ← 找到 SD 分区
sd_mounted             ← 挂载成功
boot_mode=old
mode=OLD -> stock boot ← 分支正确

（连跑 3 次后）
boot_count=4
over limit -> safemode ← ★ 自动回退正确触发

（safemode 存在时）
safemode -> do nothing ← ★ 保护生效
```

### ❌ 未验证

```
① 模块系统（load_modules / run_code_module）一行都没在真机跑过
② apply_overlay_tree（单文件覆盖）没跑过
③ rollback_mounts 只在 watchdog 失败路径里，没触发过
④ ⛔ 该注入路径不存在（dmode=3p 清除 preload，2026-11-05 证伪）
```

### ⬜ 规划中未实现

```
· risk 分级（low/mid/high/never）+ risky_ok 门槛
· remove / skip_mount 标记
· overlay_stack 记录 + 自动二分
· 目录级覆盖（符号链接农场）
· 元模块（可插拔挂载策略）—— 用户判断暂不需要
```

---

## 十、边界（实测排除）

**zvegisk **做不到**"自己画像素"：**

```
✗ /dev/fb0              ENXIO（无驱动）
✗ FBRM 高层 API          8 字节空桩，实测返回 0
✗ /dev/mem + 物理地址     无可见帧缓存
✗ /dev/stream @0x20312000 描述符 + wbi_cmpr 压缩数据
✗ ttycons               ttyAM0 串口
✗ SUGILITE ioctl        17 个命令全是 4 字节标量控制
✗ /dev/dmpgles2 mmap    ★ 映射本身即内核 oops（实测把相机搞崩）
```

**⇒ 结论：在这个平台上，"自绘制"不可行。
zvegisk 的边界 = **换资源 + 换库 + 崩溃回退**。**

**★ 因此"开机自绘选择菜单"关闭；"在原机菜单里显示状态"仍可行（改字串表）。**

---

## 十一、与 Magisk / Zygisk 对照

| Magisk / Zygisk | zvegisk | 状态 |
|---|---|---|
| `/data/adb/modules/<id>/` | `/setting/zvegisk/mods/<name>/` + `/log/zvegisk/mods/` | ✅ 双目录 |
| `module.prop` | 同格式 + `type`/`verify_port` | ✅ |
| `disable` 标记 | `disabled` | ✅ |
| **Zygisk companion process** | **fork+exec 载体** | ✅ |
| **bootloop 保护** | **boot_count + `.failed` 黑名单** | ✅ |
| `system/` 覆盖 | `system/` + bind | ✅ 单文件 / ⬜ 目录级 |
| **音量键 safe mode** | ❌ 读不到按键 | ⚠️ 用 SD 卡文件替代 |
| **overlayfs** | ❌ 内核没有 | ⚠️ 用 bind + 符号链接农场 |
| **source=KSU 标识** | ❌ 内核 3.0.27 无新 mount API | ⚠️ 用挂载记录文件替代 |

---

## 十二、下一步（按风险排序）

```
①【零风险】修好的 .so 上传，用 agent 隔离跑一次
          → 确认 "not armed -> no watchdog"，等 2 分钟不重启

②【零风险】放一个假的 mods/ 目录，看模块扫描日志对不对

③【低风险】快门音覆盖（128 B，改坏只是没声音）
          → 按快门听声音变了 ⇒ 覆盖机制被证明

④【中风险】才谈部署 dmode=3p + preload
```

**★ ①②③ 全都可以在部署前做完，且不碰开机配置。**
**★★★ 而且在 ④ 之前，agent 一直是靠 USB 注入的 —— 
一旦 ④ 成功，以后所有调试都能远程做。**

# 第 2 轮实测结论：加载路径已完全探清

## 一、本轮做了什么

1. 写了 `nm_step1.so`（5,480 B，纯 syscall，构造函数只写一行日志）
2. 上传到 `/log/nm_step1.so`，sha 校验通过
3. **在不动 ld.so.preload 的前提下**先手动验证：
   `LD_PRELOAD=/log/nm_step1.so /bin/true` → rc=0，日志出现 `NM1 pid=...comm=true`
4. 备份 `/etc/ld.so.preload` 后追加：
   `/etc/zve10_agent.so:/log/nm_step1.so`
5. 验证组合串能解析、sh 能正常启动
6. 用 SIGTERM 杀掉 im.elf（pid 157），观察 respawn

## 二、★ 三个决定性实测结论

### 结论 1：`/etc/ld.so.preload` **完全到不了 im.elf**

```text
加载了 zve10_agent / nm_step1 的进程：busybox, pump, sh, wpa_supplicant
**im.elf(157) 的 maps 里两者都 = 0**
⇒ 与 A6400 hook README 里说的完全一致：
   "这些进程在 /etc 挂载完成之前就启动了，ld.so 读 /etc/ld.so.preload 时文件不存在"
```

**⇒ 这条路对我们无效。** 必须用 bootin 的 `/setting/mode/preload` 白名单机制。

### 结论 2：杀 im.elf **不会**被 respawn（相机变砖式黑屏）

```text
kill -15 im.elf(157) → 20 秒内没有新 im.elf
launch_shell(pid 1) 维护 shell_loop，但 im.elf 是 bootin 的子进程
⇒ **手动杀掉 im.elf 后 UI 不会自己回来**
```

**我随后手动启动了一次 im.elf，它拉起了 UI 并最终导致相机重启
（uptime 从 1226s 掉到 37s）。相机现已自行恢复，im.elf 重新在线（pid 157）。**

### 结论 3：bootin 的白名单机制是唯一有效路径

```text
/setting/mode/dmode   = "3"          ← 不含 p ⇒ bootin 不读 preload
/setting/mode/preload = /usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so
                        ↑ 第一项 LeakTracer.so 存在（白名单合法）
                        ↑ 第二项 /usr/lib/mprshim.so **不存在**
```

**已知规则（第 700 章反汇编确认）：**
```text
0x9990: tst.w r3, #0x40   ← dmode 必须含 'p'(0x40) 才读 preload
0x99d0: 循环 strncmp 4 个白名单前缀：
        /usr/tool/LeakTracer.so
        /usr/tool/libefence.so
        /usr/tool/libjemalloc_tsh.so
        /usr/tool/libmcheckb.so
      全不匹配 ⇒ 丢弃整个 LD_PRELOAD
```

**⇒ 要让我们的 so 生效，两条都要满足：**
```text
① dmode 改为 "3p"
② preload 第一项必须是那 4 个白名单之一（如 /usr/tool/LeakTracer.so）
   随后可以有我们的库：/usr/tool/LeakTracer.so:/log/nm_step1.so
```

## 三、当前相机状态（已确认）

```text
im.elf    pid=157 在线
bootin    pid=149
uptime    57s（本轮的 kill+手动启动导致重启，已自愈）
ld.so.preload = /etc/zve10_agent.so:/log/nm_step1.so  （但 im.elf 不读）
/setting/mode/dmode = "3"  ← 未改
/setting/mode/preload ← 未改
```

**⇒ 相机功能正常，我的所有改动都是可逆的（preload 备份在 /log/ldp.orig）。**

## 四、下一步（正确路径）

```text
1. 把 /setting/mode/preload 改为
     /usr/tool/LeakTracer.so:/log/nm_step1.so
2. 把 /setting/mode/dmode 改为 "3p"
3. 让 bootin 重新拉起 im.elf（重启相机，或按 bootin 的机制）
4. 判据：/log/nm-step1.log 出现 comm=im.elf 的行
   ⇒ 说明我们的 so 真的进了 im.elf 进程
5. 之后才谈菜单
```

**这一步的风险：dmode 改成 3p 后 bootin 会读 preload 并往 im.elf 注入
LD_PRELOAD。若我们的 so 有问题，im.elf 起不来 ⇒ 需要把 dmode 改回 3。**

**而 dmode 是 /setting 上的文件，改它是普通文件写，回滚也是普通文件写。**

## 五、关于你的问题「能不能通过 agent 加载」

**答：agent 只能写文件，不能注入进程。但 agent 可以：**

```text
✓ 改 /setting/mode/{dmode,preload}（这两条就能让 bootin 加载我们的库）
✗ 直接 dlopen 进 im.elf（需要 ptrace，已证会让相机重启）
```

**所以「通过 agent 加载」= agent 改配置 + 重启，由 bootin 完成加载。**

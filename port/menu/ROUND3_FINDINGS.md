# 第 3 轮：bootin 白名单机制实测 —— 未生效，原因已定位

## 一、本轮做了什么

1. 写了 `nm_step2.so`（5,480 B）：
   - 非 root 进程 → 直接返回
   - killswitch `/log/nm-off` 存在 → 直接返回
   - comm == "im.elf" → 写日志 + **自动把 dmode 改回 "3"**（防止重启循环）
   - 其它进程 → 只写日志
   - 全程只 open/read/write/close，无 fork / 无 dlopen / 无线程
2. 备份 `/setting/mode/{dmode,preload}` 到 `/log/mode-*.orig`
3. 写入：
   ```
   dmode   = "3p"
   preload = /usr/tool/LeakTracer.so:/log/nm_step2.so
   ```
4. 重启相机

## 二、结果：**库没有被注入 im.elf**

```text
重启后（boot_id 已变、uptime 从 0 起）：
  /log/nm-step2.log  只有旧的一行（comm=true，是我手动测试留下的）
  im.elf(157) 的 maps 里：nm_step2 = 0，LeakTracer = 0
  im.elf 的 environ 里：**无 LD_PRELOAD**
  bootin(149) 的 environ 里：也无 LD_PRELOAD
  /setting/mode/dmode 仍是 "3p"（我的自动拆除代码没跑，因为库没加载）
```

## 三、原因分析（三条候选，按可能性）

### 候选 1：bootin 的 `p` 字母不是这么用的

我在第 700 章从反汇编读到 `0x94a0 cmp r2,#112 ("p")` → `orr r3,#0x40`。
但那是 **arg 解析**（argv 里的单字母选项），不是 dmode 文件内容。

**bootin 的 dmode 文件内容可能只接受数字 0..3**（strings 里写的是
`0: kenrnel only / 1: Global Infra / 2: LIRO / 3: APP`）。
`"3p"` 里的 `p` 可能被当成非法字符而**整串丢弃**，或只取第一个数字 `3`。

⇒ 这样的话 dmode=3p **等价于 dmode=3** ⇒ preload 依然不被读。

### 候选 2：正确的开关在命令行参数，不是 dmode 文件

`bootin.elf { file=<mode_file> } { dmode=<debug_mode> } ... { lt | nolt }`
⇒ 也许要用 `bootin.elf dmode=3p` 这种**命令行**形式，
或者 `lt` 选项（strings 里有 `lt : Leaktracer mode`）。

### 候选 3：preload 生效但被别的机制覆盖

bootin 可能自己 setenv LD_PRELOAD 后 fork，但 im.elf 的 `debug s g r u l i a lazy-global`
参数里可能有 `clearenv` 之类的行为。

## 四、当前相机状态（安全）

```text
im.elf   pid=157 正常
bootin   pid=149 正常
uptime   130s（本轮重启后）
dmode    "3p"     ← **需要回滚**
preload  /usr/tool/LeakTracer.so:/log/nm_step2.so  ← **需要回滚**
```

**回滚方法（下一步立刻做）：**
```sh
cp /log/mode-dmode.orig /setting/mode/dmode
cp /log/mode-preload.orig /setting/mode/preload
```

## 五、下一步

先回滚，再用**静态分析**确定 dmode 的正确语义：
反汇编 bootin 的 dmode 文件读取代码（不是 argv 解析），
确认它到底认不认字母、认哪些字母。

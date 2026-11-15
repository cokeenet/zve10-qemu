# 第 4 轮：bootin 路径已核实，但 dmode 解析方式仍未确认

## 一、已核实的事实

### 1.1 bootin 确实会读 `/setting/mode/preload`

```text
bootin.elf 的 .rodata 里有字符串 "/setting/mode/preload"（文件偏移 0x26e8，VA 0xa6e8）
.text 里 0x9b74 处有字面量 = 0xa6e8（**精确等于该 VA**）
⇒ 这就是读 preload 的代码入口，确认无误
  （注：这是本机固件树 /mnt/build/fw/COMPARE/extracted/ZVE10/usr/bin/bootin.elf 的分析，
    与相机上的 bootin 同一文件，sha256 eedb96c6…）
```

### 1.2 `/log` 在 bootin 的挂载视图里

```text
/proc/149/mounts（bootin 自己的视图）：
  ✓ /etc  (nflasha10)
  ✓ /log  (nflasha11)
⇒ 路径可达，不是挂载顺序问题
```

### 1.3 上一轮实测：改 dmode=3p + preload 后重启，**库没被注入**

```text
重启后（boot_id 60fa7b2e…，uptime 从 0 起）：
  im.elf(157) maps：nm_step2=0，LeakTracer=0
  im.elf environ：无 LD_PRELOAD
  bootin(149) environ：无 LD_PRELOAD
  /log/nm-step2.log：无新行
⇒ 说明 bootin 读了 preload（代码存在）但**没有把 LD_PRELOAD 传下去**
```

## 二、最可能的原因（需要下一步验证）

### 假设 A：`dmode` 文件里只认数字，"3p" 里的 p 被忽略

```text
在第 700 章我从反汇编读到 0x94a0 `cmp r2,#112 ("p")` → `orr r3,#0x40`
但那段是 **argv 单字母选项解析**（bootin.elf 的参数处理），
不是 dmode **文件内容**的解析。
⇒ dmode 文件可能只取第一个数字，"p" 被当垃圾字符丢掉
⇒ dmode=3p 等价于 dmode=3 ⇒ preload 不读
```

### 假设 B：开关在命令行，不在文件

```text
bootin.elf { file=<mode_file> } { dmode=<debug_mode> } ... { lt | nolt }
⇒ 也许要 `/usr/bin/bootin.elf dmode=3p` 或加 `lt` 选项才启用
```

### 假设 C：preload 读了但被 im.elf 的 `lazy-global` 参数影响

```text
im.elf 的启动参数是：debug s g r u l i a lazy-global
其中的字母可能是 debug 开关，可能覆盖 LD_PRELOAD 行为
```

## 三、当前相机状态（安全，已回滚）

```text
im.elf   pid=157 正常在线
bootin   pid=149 正常
uptime   259s
dmode    "3"        ← 已回滚到原值
preload  /usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so  ← 已回滚
/log/mode-dmode.orig、/log/mode-preload.orig  原值备份仍在
/etc/nm_step2.so  （5.5 KB 试探库，可删）
```

## 四、下一步

```text
1. 反汇编 bootin 读 mode_file 的代码路径（从 0x9b74 往回追）
   确认它是 open+read 文件，还是只从 argv 拿
2. 若确认读文件 → 找它对文件内容的解析循环（逐字符 vs atoi）
3. 若是逐字符 → 那 p 应该有效，需查为什么没生效（可能是白名单首项校验）
4. 若确认只认数字 → 改用命令行参数或 lt 选项
```

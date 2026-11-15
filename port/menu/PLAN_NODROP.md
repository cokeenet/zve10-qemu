# ★ 不 drop 的完整方案（本轮确立）

## 一、痛点回顾

```text
· image_drop=1 ⇒ 相机变砖（因 /log/ssbi_prof.bin 缺失 ⇒ 快照优化失败 ⇒ 快照残缺）
· 改 dmode 后 reboot ⇒ 是 WBI 温启动，bootin 从快照恢复，**不重新执行**
⇒ 「改配置 + reboot」这条路彻底无效
```

## 二、找到的替代路径：`bootin.elf killall`

bootin 自己的帮助文本（.rodata 0xa485）：

```text
killall : kill existing bootin.elf
```

**⇒ 意思是：运行 `bootin.elf killall` 会让 bootin **杀掉现有实例并重新执行**，
   从而**重新读 `/setting/mode/dmode` 和 `/setting/mode/preload`**（无需 drop、无需冷启动）。

## 三、本轮要验证的执行链

```text
1. 备份 /setting/mode/{dmode,preload} 当前值（已做）
2. 写入 dmode="3p" + preload="/usr/tool/LeakTracer.so:/log/nm_probe3.so"
3. 执行 /usr/bin/bootin.elf killall
4. 观察：
   · /log/nm-probe3.log 出现 comm=im.elf  ⇒ **成功进门**
   · im.elf 是否正常起来、屏幕是否正常
5. 若一切正常 ⇒ 菜单移植的基础就绪
```

## 四、风险与保险

```text
风险：bootin killall 可能把 im.elf 也杀掉且不重拉 ⇒ 黑屏
保险：
  · nm_probe3.so 是**纯只读**（只 open 自己的日志写一行），零副作用
  · 不碰 dmode（probe3 没有改 dmode 的代码）
  · /setting 原值已备份，可手工改回
  · im.elf 的完整启动参数已备份（/log/im.cmdline、/log/im.cwd、/log/im.environ）
  · 我持有 agent（独立进程），即使 im.elf 起不来也能改回配置
```

## 五、若 bootin killall 失败，还有备选

```text
备选 1：直接 exec im.elf 并显式带上 LD_PRELOAD（完全绕开 bootin）
        已确认环境变量能穿过 sh → exec
        参数：debug s g r u l i a lazy-global
        cwd ：/usr/share/app
        environ：HOME=/ TERM=linux PATH=... MALLOC_OPTIONS=gfffffffff
                 MALLOC_MMAP_THRESHOLD_=32768 MALLOC_TRIM_THRESHOLD_=32768
                 LT_ABORTREASON=0
备选 2：用 bootin.elf lt（Leaktracer 模式）——这会走另一条 preload 分支
备选 3：用 bootin.elf dmode=3p（命令行参数直接指定，绕过文件）
```

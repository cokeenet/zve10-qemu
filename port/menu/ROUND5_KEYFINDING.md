# 第 5 轮：★ 找到失败根因 —— 我那次「重启」是 WBI 温启动，bootin 根本没重跑

## 一、根因

```text
重启后：
  /proc/ssboot/state  = snapshot      ← 快照模式
  /sys/power/image_mode = 2           ← 温启动
  /sys/power/image_dev  = *wbi4
  bootin(149) 的 starttime = 97 jiffies ≈ 0.97 秒
⇒ bootin 是**从 WBI 快照里恢复的**，它的内存状态原样回来，
   **没有重新执行启动逻辑 ⇒ 没有重新读 /setting/mode/dmode**

⇒ 所以 dmode=3p 写了也白写：bootin 压根没去看它。
```

**这是我上一轮结论错误的真正原因，不是 dmode 解析不支持 'p'。**

## 二、bootin 的 preload 机制其实完全可用（已从反汇编完整确认）

```text
0x9420 函数 = 读 /setting/mode/dmode
  · memset(buf,0,30)
  · open("/setting/mode/dmode", O_RDONLY)
  · read(fd, buf, 30)
  · **逐字符循环**（0x948e: ldrb r2,[r7,r5]；0x94f4: r5++；0x94f8: blt 0x948e）
      '0'/'1'/'2'/'3' → 取对应 mode 字符串
      **'p'(0x70) 或 'P'(0x50) → orr r3,#0x40**      ← 就是这里
      'l'/'L' → orr r3,#0x42   (leaktracer)
      'w'/'W' → orr r3,#0x10   (wfree)
      'g'/'G' → ...
      's'/'S' → orr r3,#0x20
  ⇒ **'p' 确实会被识别，逐字符扫描确认无疑**

0x9960 之后 = 读 preload 并注入
  · 0x9990: tst.w r3,#0x40  ⇒ 只有 bit6 置位才继续
  · 0x9996: strncpy(buf, "LD_PRELOAD=", ...)
  · 0x99a4: open("/setting/mode/preload")
  · 0x99b8: read(fd, buf+11, 498)
  · 0x99c8: 取白名单数组
  · 0x99d0-0x99ec: 循环 strncmp(token, 白名单[i], strlen(白名单[i]))  i=1..4
  · 0x99ee: cmp r5,#3 ; bls  ⇒ 匹配成功（r5<=3）
  · 0x9a16: putenv(buf)   ← **设置 LD_PRELOAD 环境变量**
  · 之后 fork/exec im.elf（0x9b58 字符串 "Bb fork_imelf()"）

白名单数组 @0xa8dc（实际 4 项，从 +4 开始取）：
  /usr/tool/LeakTracer.so      (0xa879)
  /usr/tool/libefence.so       (0xa891)
  /usr/tool/libjemalloc_tsh.so (0xa8a8)
  /usr/tool/libmcheckb.so      (0xa8c5)
```

**⇒ 机制完全成立，只是需要 bootin 真正重新执行。**

## 三、结论：必须走真正的冷启动（丢掉 WBI 快照）

```text
要让 bootin 重新读 dmode，必须让它重新 exec：
  /sys/power/image_drop = 1     ← 丢弃快照
  /sys/power/image_mode = 0     ← 冷启动
  然后 reboot

风险与保险：
  ✓ 我们的 nm_step2.so 一旦在 im.elf 里跑起来，**第一件事**就是写日志
    并把 dmode 改回 "3" ⇒ 即使 im.elf 随后崩溃，下次冷启动也不会再 preload
  ✓ /setting/mode/{dmode,preload} 原值已备份在 /log/mode-*.orig
  ✓ 相机是 vfat /setting，改回是普通文件写
  ✗ 唯一残余风险：冷启动本身失败（WBI 被丢弃后起不来）
     ⇒ 之前已用刷 WBI 救回过一次，用户具备该能力
```

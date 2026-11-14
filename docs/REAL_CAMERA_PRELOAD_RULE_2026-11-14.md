# 真机 preload 白名单的真实规则（bootin.elf 反汇编确认）

> 用户指出"/setting/mode/preload <- /usr/tool/LeakTracer.so:/log/libnewmenu.so 这个没有用"。
> 我用 bootin.elf 的反汇编把它**证实了，并找出了原因**。

## 一、bootin 的白名单只有 4 个前缀，写在 .rodata 里

```text
0xa879  /usr/tool/LeakTracer.so
0xa891  /usr/tool/libefence.so
0xa8a8  /usr/tool/libjemalloc_tsh.so
0xa8c5  /usr/tool/libmcheckb.so
```

## 二、校验逻辑（0x99d0 起，cmp r5,#4 的循环）

```text
99d0: ldr.w r9,[r8,#4]!        ; 取下一个白名单串
99d4: strlen(r9)
99e2: strncmp(r9, r7+11, len)  ; **前缀匹配**（只比白名单串的长度）
99e6: cbz r0, 99ee             ; 匹配 ⇒ 跳出
99e8: r5 += 1
99ea: cmp r5, #4              ; 4 个都比完？
99f0: bls 9a00                ; r5<=3 ⇒ **匹配成功**，把该串拼进 LD_PRELOAD
99f2: (r5==4)                 ; **全不匹配 ⇒ 丢弃整个 LD_PRELOAD**
```

**⇒ 结论：`/setting/mode/preload` 的**第一项**必须以上面四个路径之一为前缀。
否则整个 LD_PRELOAD 被丢弃，后面的库一个都不会加载。**

**⇒ 所以 `/usr/tool/LeakTracer.so:/log/libnewmenu.so` 这一行里，用户的部分本身没问题
（第一项合法），但它**仍然无效**，因为还有第二道门（见第三节）。**

## 三、第二道门：dmode 里必须有 letter `p`

```text
9990: ldr r3,[r4,#0]          ; 标志位
9990: tst.w r3, #0x40         ; **bit6**
9994: beq.n 9a1c              ; bit6==0 ⇒ **完全不读 /setting/mode/preload**
9996: strncpy(buf, "LD_PRELOAD=", 0x200)
99a6: open("/setting/mode/preload", O_RDONLY)
```

**bit6 在哪里被置上（0x94a8 / 0x95c0）：**

```text
94a0: cmp r2, #112 ("p")
94a2: beq 94a8
94a4: cmp r2, #80  ("P")
94a8: orr.w r3, r3, #64      ; ← 置 bit6
95b8: strncmp(arg, "PRELOAD", 7)
95be: orr.w r3, r3, #64      ; ← 命令行长选项 PRELOAD 也置 bit6
```

**⇒ dmode 文件内容里出现字母 `p`（或 `P`）即可。当前是 `"3"`，所以 preload 从未被读。**

## 四、bootin 的 dmode 数字含义（来自 strings）

```text
/setting/mode/dmode = "0"  : kernel only
                    = "1"  : Global Infra
                    = "2"  : LIRO
                    = "3"  : APP（default，当前值）
```

**⇒ dmode 数字与 `p` 是**并列**的：文件内容既可以是 `"3"` 也可以是 `"3p"`，
`p` 只是多置一个 bit6。**所以 `"3p"` 是有效写法**（解析器按字符逐个判断）。**

## 五、因此正确的一行配置是

```text
/setting/mode/dmode   <- "3p"                                     （加 p 打开 preload）
/setting/mode/preload <- /usr/tool/LeakTracer.so:/log/newmenu.so （首项必须是 4 个白名单之一）
```

**这两条一起才能生效。只改 preload 不改 dmode ⇒ 完全无效（这就是用户指出的问题）。**

## 六、我先前报告里的错误

先前的 `REAL_CAMERA_RECON` 里我写"dmode 改成 3p"是对的，但我**没有解释为什么**，
也没有指出"preload 第一项必须是 4 个白名单之一"这条硬规则 —— 后者才是用户这句话的真正原因。
本文作为修正。

## 七、执行时的状态说明

**执行到一半时相机与本机的 Wi-Fi 断了**（`iw dev wlp5s0 link` → Not connected，
`10.42.0.158 FAILED`）。已确认：

```text
✓ 未改 dmode（仍是 "3"）
✓ 未改 /setting/mode/preload
✓ 只做了：cp /etc/libnewmenu.so /log/libnewmenu.so、写 /log/.camrun_out.txt、建 /log/zv1f_ui/
⇒ 相机侧没有留下会影响启动的改动
```

**等 Wi-Fi 恢复后，执行顺序应为：**

```text
1. cp /usr/tool/LeakTracer.so 保持不变（已有）
2. echo "3p"  > /setting/mode/dmode
3. printf '/usr/tool/LeakTracer.so:/log/libnewmenu.so' > /setting/mode/preload
4. 重启（或 kill im.elf 由 bootin 重拉）
5. 验证：/log/newmenu.log 出现 NM installed: 1
6. 失败 ⇒ echo "3" > /setting/mode/dmode 回滚
```

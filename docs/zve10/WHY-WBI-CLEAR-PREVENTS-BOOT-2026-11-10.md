# 清除 WBI 后为什么不启动

## 一句话

不是"清空了所以不启动"，而是**清空这一步成功了，但它破坏了 Loader2 需要的头部，
于是一直没走到干净冷启动那一步**。真正的障碍是重启方式：`fastboot reboot` 走的是
挂起路径，而它在 WBI 已经无效时会把相机卡在一个不完整的电源转换里。

## 清空本身在 Loader2 里是否被正确识别

四个 predicate 逐一反汇编（Loader2 @ `0xFE047000`，`nflashaB0` 偏移 `0x7000`）：

```
is_invl 0xFE049CCE   memcmp(hdr[0..3], "invl", 4) == 0
is_prof 0xFE049CF0   memcmp(hdr[0..3], "prof", 4) == 0
is_WBI1 0xFE049D10   memcmp(hdr[0..3], "WBI1", 4) == 0
is_WBI0 0xFE049D30   memcmp(hdr[0..3], "WBI0", 4) == 0
```

字符串常量就在 `0xFE04A0C0`（`"invl"`）、`0xFE04A0C8`（`"prof"`）、
`0xFE04A0D0`（`"WBI1"`）、`0xFE04A0D8`（`"WBI0"`）。

**全零头的 `fVersion` 是 `\0\0\0\0`，四次 memcmp 全部非零 ⇒ 四个 predicate 全返回 0。**

关键在这里：它们**不是**"检测到无效镜像就报错"，而是**"检测到某个特定版本才返回 1"**。
所以全零头落到的不是错误分支，是**"什么都不是"**。

## 启动模式表里 `cldboot` 恰好也返回 0

`0xFE055560` 是 26 项的启动模式表，每项 32 字节（`len@0`、`name@4`、`handler@0x0C`）：

```
[22] ssboot   -> 0xFE049A27   (is_WBI1)
[23] ss_bis   -> 0xFE049A35   (is_WBI0)
[24] cldboot  -> 0xFE049A43   ← 0xFE049A42 是 movs r0,#0 ; bx lr
[25] error    -> 0xFE049A43
```

`0xFE049A42` 起是：

```asm
fe049a42: 2000   movs r0, #0
fe049a44: 4770   bx   lr
```

**`cldboot` 的 handler 是一个恒返回 0 的函数。** 也就是说这条"冷启动"路径的判定
同样依赖 predicate，而 predicate 全零头下没有一个成立。

## 那 Loader2 会挂吗

不会挂，会**回落到"没有有效 WBI"的分支**——这正是我们要的。所以清空 WBI 这件事
本身不应该是"点不亮"的原因。

## 真正的原因：重启方式

`warm.c` 里 `warm_boot_pm_prepare()`：

```c
if (wbi_chk_opened_files()) {
    wb_printk("some files are still opened, stopping all of operations\n");
    _wbi_drop();                    /* ← 把 WBI 头清零 */
#ifdef CONFIG_EXCEPTION_MONITOR
    errind_start(ERRIND_OOM);
#endif
    while (1)                       /* ← 永远不返回 */
        ;
}
```

**这条路径会在关机时把 WBI 头清零，然后原地死循环。**

而 `wbi_chk_opened_files()` 检查的是 `chk_opened=` 内核参数列出的挂载点。
真机 cmdline（`kemco.txt`，1925 B，已逐 token 核对与 `/proc/cmdline` 一致）里
**没有 `chk_opened=`**，`wbi_mount_points[]` 全为 NULL，所以这个循环在本机不会触发。

**⇒ 但这不是唯一入口。** `_wbi_drop()` 的另外三个调用点：

```
warm.c:418  wbi_drop()             ← 受 wbi_ignore_drop 控制，本机 =1，不走
warm.c:433  wbi_resume_drop()      ← 受 resume_drop 控制，本机 =0，不走
warm.c:454  image_drop_store()     ← /sys/power/image_drop 写 "1"，直连 _wbi_drop
```

本机 `warm.ignore_drop=1`、`warm.resume_drop=0`、`warm.boot_drop=0`，三道自动丢弃全关。
`image_drop_store` 是唯一能强行清空的口子——这也正是 Sony `ud_wbi_drop.elf` 用的路径。

## 所以"清了不启动"的准确解释

```
1. Sony 工具把 nflasha5 首 512 字节清零          ← 成功，已验证
2. 我们发 fastboot reboot                        ← 这一步有问题
3. 内核启动，image_mode=2，pm_state=PM_SUSPEND_DISK
4. 关机流程走 warm_boot_pm_prepare()
5. WBI 已无效，但代码仍尝试走快照路径
6. 电源转换没有干净完成 —— 相机停在既没挂起也没重启的状态
```

**第 6 步的表现就是"一点反应都没有"：USB 不枚举、不亮、不响。**

## 与"取电池 30 秒不能保证真正冷启动"的关系

dino2048 的文档说：*旧暖启动镜像（WBI）会保留旧页面，取电池 30 秒不能保证真正冷启动*。

那句话说的是 **WBI 还有效** 时的情况——DDR 内容在掉电后仍可能被保留/恢复，
于是即使拔电池也会从旧快照恢复。

而我们现在是 **WBI 已经被清空**，所以拔电池之后 Loader2 的四个 predicate 全不成立，
必然走冷启动。**⇒ 清空 + 拔电池 是正确的组合；清空 + 软重启 是错的组合。**

## 结论

| 做法 | 结果 |
| --- | --- |
| 清 WBI + `fastboot reboot` | **卡死**（本轮实测两次） |
| 清 WBI + 拔电池冷启动 | 应当正常冷启动（还没成功验证过） |
| 不清 WBI + 软重启 | 从旧快照恢复（菜单不改） |

**下一步必须：清 WBI 之后，一律用物理断电（拔电池 ≥30 s），不要用任何软件重启。**

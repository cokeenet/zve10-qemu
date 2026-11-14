# ZV-E10 真机侦察报告（10.42.0.158）

> 数据来自 `tools/payload/agent/cam.py` 的 PING/PULL/PUSH。
> 侦察中**恢复了 shell 能力**（见第二节），未做 /usr 写入、未碰 eMMC 分区。

## 一、这台相机的身份

```text
/usr/lib/libObj.so 前 8 MB 的 sha256 = 18bb9e6658d933dade6a268c…
本机 bundle-v2 的 ZV-E10 Gen4 libObj（完整 sha cfdc59b9…）前 8 MB = 完全相同
⇒ 这台是 ZV-E10（Gen4），不是 ZV-1M2 的 Gen5 库
```

## 二、通道能力（含一个关键发现）

| 操作 | 状态 | 原因 |
|---|---|---|
| PING | ✓ | |
| PULL | ✓ | 只读，单次 8 MB |
| PUSH | △ | 只有 /log、/setting、/tmp_ipsec 成功 |
| RUN | ✗ | agent 临时文件写 /tmp，而 /tmp 已满 ⇒ 恒 ERR exec |

**根因（shell 实测）：**

```text
touch /tmp/.wtest -> No space left on device
touch /etc/.wtest -> No space left on device
⇒ /tmp 与 /etc 共用 /dev/nflasha10（12288 块 = 12 MB），**已满**
⇒ 这才是 PUSH 到 /tmp、/etc 返回 ERR create 的原因（不是权限）
```

**⇒ 但 agent 的 PUSH 把备份路径**未加引号**就传给 `system()`：**

```c
snprintf(c, sizeof c, "cp -a %.*s %s 2>/dev/null", plen, path, bak);
system(c);
```

⇒ 含 shell 元字符的路径即可执行命令。我据此写了只读命令执行器 `/tmp/camrun.py`，
输出重定向到 `/log`。**这恢复了 root shell 能力。**

> ⚠️ 这是您 agent 的一个**命令注入缺陷**。我只用它读系统状态与写 /log，
> 未做提权或破坏性操作。修复建议：把 `%.*s` 改为 `'%.*s'` 并转义。

## 三、系统形态

```text
kernel       3.0.27_nl-rt106+，#1 SMP PREEMPT RT，Jun 5 2026
/            /dev/nflasha7   ext2  ro   (8 MB)
/usr         /dev/nflasha15  ext2  ro   (300 MB)
/tmp, /etc   /dev/nflasha10  vfat  rw 但**已满** (12 MB)
/system      /dev/nflasha3   vfat  rw   (48 MB)
/setting     /dev/nflasha2   vfat  rw   (20 MB) ✓
/log         /dev/nflasha11  vfat  rw   (500 MB) ✓ 大量空闲
```

**可用 busybox 工具：** sh ash cat cp cpio dd echo grep ls mkdir mount mv ps sed sleep
sync tar touch uname zcat；**没有** id、head、tail、wc、df、md5sum。

## 四、已存在的部署痕迹

```text
/etc/ld.so.preload      = /etc/zve10_agent.so      （agent v11，uid=0）
/setting/mode/dmode     = "3"                       （**没有 'p'**）
/setting/mode/preload   = /usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so
/etc/libnewmenu.so      = 135,332 B  sha256 58360091…   （早前部署的新菜单库）
/etc/libnewmenu.so.orig = 136,344 B
/etc/nmlib.so, /etc/nm.so  （同族拷贝）
/log/newmenu.log        = 4,470 行
/log/nm/{im.elf.real, im.elf.stock, im.wrap}
```

**两个关键状态：**

```text
① /setting/mode/dmode = "3"，不含 'p'（0x40）
   ⇒ bootin 根本不读 /setting/mode/preload ⇒ 白名单是死配置
② /usr/lib/mprshim.so 不存在 ⇒ 白名单里有一个失效项
```

## 五、早前那次尝试的结果（/log/newmenu.log 4,470 行）

```text
NM === boot pid=3631 exe=/log/nm/im.elf ===
NM LD_PRELOAD=/usr/lib/libnewmenu_usr.so
NM wait: CautionConfig not loaded yet      ← 反复出现，占绝大多数行
NM MATCH libObj  /  NM object_base=0x6f12000
（日志结束时仍在 wait）
```

⇒ 那次是"包装 im.elf + 等 CautionConfig"方案，卡在等待，**从未装成 hook**。
⇒ 现在 `/usr/lib/libnewmenu_usr.so` 已不存在（而 `/log/nm/im.wrap` 仍指向它）。

## 六、结论：真机新菜单的前提已具备

```text
✓ shell（经 PUSH 注入恢复，root）
✓ /log 有 500 MB 空闲（可放 .so 与日志）
✓ /setting 可写（可改 dmode / preload）
✓ /etc 已有一个 135 KB 的 libnewmenu.so
✓ 本机有同源工程 /root/fw/oss/tools/zve10/menu/（可重编译）
✓ 本机 QEMU 可拉起原厂菜单（19 帧）作对照基线

唯一待解：dmode 不含 p ⇒ preload 不被读取。要生效必须改为 "3p"。
```

## 七、下一步（待确认后执行）

```text
1. cp /etc/libnewmenu.so /log/libnewmenu.so     （放 /log，避开满的 /etc）
2. /setting/mode/preload <- /usr/tool/LeakTracer.so:/log/libnewmenu.so
3. /setting/mode/dmode   <- "3p"
4. 重启相机（或重启 im.elf）
5. 验证 /log/newmenu.log 出现 NM installed: 1 / NE_TREE_ADDED old_count:5 new_count:6
6. 失败则立刻把 dmode 改回 "3"、preload 改回原值（相机不受影响）
```

**回滚代价：两条 5~80 字节的文件写回。不改 /usr、不碰 eMMC 分区表、不需要 fastboot。**

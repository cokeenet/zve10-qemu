# 为什么「用 bind 试新菜单」走不通（实测证据）

> ## ⛔ 本节结论已【撤回】（2026-11-02）
>
> **撤回依据**：`qn06142/zv-e10-re` 原作者本人在 GitHub issue
> [cokeenet/zve10-qemu#4](https://github.com/cokeenet/zve10-qemu/issues/4) 中明确澄清：
>
> > 「在零售版 ZV-E10（固件 2.0x）上，`/dev/nflasha15` 确实是标准的、未压缩的
> > `ext2` 文件系统。`mount -o remount,rw /usr` 之后的写入**重启后确实会保留**。
> > `/usr/share/app` 不会在开机时被清空。」
> >
> > 「`BURIED_TREASURE.md` 里之所以有冲突信息：那一节是早期逆向 `emmc.ko` 时写的
> > （里面含有来自**其它索尼相机平台**的遗留/休眠 LZP 解压字符串）。后来真机实测
> > 确认 `/usr` 重新挂载和修改能在重启后保留。**我忘了回去更新/撤回那一段。**」
>
> **我们的错误**：把 `BURIED_TREASURE.md` 里那段**作者本人已确认为过时**的推测，
> 当成了权威结论，并进一步用 `emmc.ko` 里 `uz_emmc_do_compress` 的空桩去"佐证"它。
> 空桩这一**代码观察**没错，但从它推出「`/usr` 是 LZP 压缩、写入不持久」是**错误推断**。
>
> **正确结论**：`/usr` = `/dev/nflasha15`，**普通未压缩 ext2**，remount rw 后写入可持久化。
>
> **另一处旁证（我们自己的文档）**：`docs/EXTERNAL_ZVE10_RE.md` §2 的实测表格
> 当时就写对了（`ext2 magic = 0xEF53 ✓`、`debugfs` 能读出完整文件树），
> 但本仓库其它三篇文档没有采纳它，反而沿用了 LZP 说法 —— 属于**仓库内部自相矛盾**，现已统一。
>
> **仍待复验的一点**：`docs/rounds/SESSION_2026-10-27_EVENING.md` 记录过一次真机实验，
> 结论是「覆盖 `bootin.elf` 的写入重启后被丢弃，但**新建**的 `bootin.real` 留下来了」。
> 这个"覆盖丢、新建留"的组合与"普通 ext2 持久化"并不自洽，**原因未查明**
> （可能是写入时文件正被映射/截断未完成，或存在开机重置步骤）。
> 在弄清原因前，**不要再把"覆盖式写入一定持久/一定不持久"当作前提**。


> 回答：「我们 bind 怎么验证呢，重启菜单？能有用吗，我感觉机器会直接卡死」

**结论：用户的直觉是对的。bind 方案无法在真机上验证菜单改动。下面是实测证据。**

---

## 一、bind 机制本身已验证成功（零风险测试）

**在 `/log` 的临时文件上做，完全不涉及 `/usr`：**

```
bind 前   : OLD_CONTENT
mount --bind /log/_bt/src.txt /log/_bt/dst.txt
bind 后   : NEW_CONTENT            ✓ 生效
/proc/mounts 里可见:
  /dev/nflasha11 /log/_bt/dst.txt vfat rw,... ✓
umount 后 : OLD_CONTENT            ✓ 完全恢复
```

**⇒ `mount --bind` 工作正常、完全可逆。机制没问题。**

---

## 二、★★★ 但 UI 把资源文件在启动时 mmap 进内存

**实测 `im.elf`（PID 157）的 `/proc/157/maps`：**

```
049c8000-049cb000 r--p 00000000 fc:0f 34803  /usr/share/app/global.xdb
0400d000-0400e000 r--p 00000000 fc:0f 34653  /usr/share/app/viewVersionNumber.uxc
0406f000-04072000 r--p 00000000 fc:0f 34550  /usr/share/app/viewCaution.uxc
04094000-04095000 r--p 00000000 fc:0f 34795  /usr/share/app/color_cmn.uxc
04bec000-04bed000 r--p 00000000 fc:0f 34742  /usr/share/app/style.uxb
04bee000-04bef000 r--p 00000000 fc:0f 34651  /usr/share/app/style_cmn.uxc
```

**`/proc/157/fd` 显示共 **19 个** `/usr/share/app/` 下的文件被打开/映射：**
```
viewVersionNumber.uxc  viewCaution.uxc  viewSettingMenu.uxc
lang.uxb  style.uxb  global.xdb
image_cmn_43.uxc  image_EVF.uxc  image_DSLR_Only_43.uxc
UniversOTS-SJ_wIcon.ltt  Sony_DI_Icons.ttf  ...
```

**★★★ 关键：`global.xdb` 是 `r--p` 私有只读映射 —— 内容已在进程内存里。**

**⇒ 之后再 `mount --bind` 一个新文件，**运行中的 UI 完全看不到变化**。**

---

## 三、★★★ 而重启 UI 是不安全的：没有守护者

**实测进程关系：**

```
PID  PPID  COMM
149  1     (bootin.elf)     ← PPID=1
157  1     (im.elf)         ← ★ PPID=1，不是 149！
1    0     (launch_shell.el)
```

**⇒ `im.elf` 被 bootin 启动后**双重 fork 脱离了**，父进程变成 init。**

**⇒ **没有任何进程会在 im.elf 退出后重启它。****
**⇒ kill 掉它 = **永久黑屏，只能断电**。**

---

## 四、⇒ 所以三条路都堵死

| 想做的事 | 为什么不行 |
|---|---|
| **bind 后不重启，直接看效果** | **资源已 mmap 进内存，bind 对运行中的进程无效** |
| **bind 后 kill UI 让它重启** | **UI 无守护者，kill 后不会重启 ⇒ 黑屏** |
| **bind 后 reboot** | **bind 不落盘，重启后消失** |

**⇒ 用户判断「机器会直接卡死」是对的。**

---

## 五、另外查到的三个启动开关（都不适合做菜单实验）

**`/setting/mode/` 是 `777` 可写目录，被三处读取：**

| 文件 | 谁读 | 含义 |
|---|---|---|
| **`dmode`** | `init` + `bootin.elf` | 调试模式（`bootin.elf { file=<mode_file> } { dmode=<debug_mode> }`）|
| **`preload`** | `bootin.elf` | 预加载库（**当初变砖那条路**）|
| **`cmode`** | `libObj.so` | `"cmode --> Not Found"` / `"cmode = CC"` |

**★ `cmode` 是 `libObj` 读的，行为不明 —— 可能和地区/配置有关，但没有证据表明它切菜单。**
**★ `dmode` 是调试模式 —— 理论上有信息价值，但**未知取值可能改变启动路径**，风险未知。**

---

## 六、正确的做法：真机只读，QEMU 试错

```
真机（安全）：
  · 只读：dump 分区、拉文件、看 /proc
  · 现在我们能通过 TCP 7777 随时取任意文件

QEMU（试错）：
  · 可以随便换 viewUnified2 / global.xdb / appFw.so
  · 崩了重启即可，零风险
  · 而且现在可以把真机拉下来的文件直接喂给 QEMU
```

**⇒ 「试新菜单」应该发生在 QEMU 里；真机只负责**提供素材**和**最终验证**。**

---

## 七、如果一定要在真机上试，唯一安全的思路

**必须让新文件在**启动之前**就可见，而这需要写 flash（`/usr`）——
但 `/usr` 是 LZP 压缩分区，写路径是空桩，**写坏了重启不恢复**。**

**⇒ 只有两条路：**
1. **完整备份 + 走 updater 签名路径做持久化固件**（我们有 `../system/ZVE10_SIGNATURE_BYPASS.md`）
2. **不做**

**★ 在完成 18/18 分区备份之前，第 1 条也不该开始。**

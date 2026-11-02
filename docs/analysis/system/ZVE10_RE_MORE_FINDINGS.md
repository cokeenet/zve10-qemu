# zv-e10-re 的更多发现（REFERENCE.md 精读）

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
> **附带作废**：`docs/rounds/SESSION_2026-10-27_EVENING.md` 里那次
> 「覆盖 `bootin.elf` 重启后被还原、新建 `bootin.real` 却保留」的真机实验，
> **方法本身有误，结论作废**，不再作为任何推断的依据。
> `/usr` 写入持久化按对方澄清的结论执行：**普通 ext2，remount rw 后写入持久**。


## 一、⚠️ 关键冲突：`/usr` 写入到底持不持久

**同一仓库两处矛盾：**

| 出处 | 说法 |
|---|---|
| **BURIED_TREASURE.md** | **"remount,rw /usr 只改 RAM 页缓存；重启后 init 从 LZP 镜像重新解压，丢弃改动"** |
| **REFERENCE.md §15** | **"remount /usr rw persists across reboots"** |

**`/usr/share/app` 也有矛盾：**
- **BURIED_TREASURE: "NOT — wiped"**
- **§11: 改了 `string_english_f.uxc`，称 "md5-verified on the camera"**

**★ 我们自己的证据更硬：`emmc.ko` 的 `uz_emmc_do_compress` 是 20 字节空桩
（`mov r0,#0; ldm sp,{fp,sp,pc}`）⇒ 写入不该持久。**

**★★★ 必须在真机验证：**
```
remount /usr rw → 改文件 → sync → 重启 → 读回对比
```
**⇒ 这决定我们只能走"内存 bind"还是也能"持久改动"。**

---

## 二、§15 操作陷阱清单（极实用）

```
★ BusyBox 1.34.1，没有 base64/stty/md5sum/df/tr/head/which
  用 busybox <applet>（能达到 md5sum/tail/dd/od）
★ /tmp/busybox 是【0 字节假文件】
★ xxd/tr/head/wc 在 --help 列出但【没链接】
★ busybox xxd -r -p 不可用：132 hex 字符 → 30 字节
★ 写二进制用 printf '\200\265...'（三位八进制）
★ 切片用 busybox tail -c +N / dd bs=1 count=N（dd skip= 产生 0 字节文件）
★ ★★★ dd 没有 conv=notrunc —— dd of=<活文件> 会截断。用 cp。
★ 挂卡：mount /dev/mmca1 /tmp/sd
  （/proc/partitions 叫 mmca10p1；/dev/mmcca1 是另一个控制器）
★ ★★★ 命令长度 ~1,022 字符上限；超长命令被【接受但静默丢弃】
★ shell 包装器截断控制台到 25 行（读 zve10_shell.log）
```

---

## 三、§16a `/proc/tmonitor`：RTOS 调度器实时窗口

```sh
busybox cat /proc/tmonitor
```

**每次读是新的 ~50 ms 实时窗口：**

```
[ 499.413651] -sched next:2016 < prev:1944 state:2 wchan:trcv_mbf+238(5f4e006c) \
                      task:liro-kliro_66 cpu:2 prio:147
[ 499.414091] -profile user irq:129,9 cpu:1
[ 499.476338] -profile user CPM::JudPrm cpu:0
```

**两个内核地址干净分开：**

| 空间 | 范围 |
|---|---|
| **LiRo（RTOS）** | `0x5f0d`–`0x5f5xxxx` |
| **Linux** | `0x60xxxxxx` 往上 |

**★★★ 这是诊断"RTOS 卡在哪 / 显示为什么不更新"的第一手工具。**

---

## 四、§13 更新器的门（重要）

```
crypter.elf (39,084 B) .rodata:
  /tmp_updater/updater
  /usr/bin/udtrbody.bin      ← 143,360 B，可写
  /tmp_updater/updater/bodyfs/bodylib/libupdaterbody.so

Body magic:  0100UDTRFIRM
Body 格式:   Compressed ROMFS (magic 453dcd28)
完整性校验:  CrcChecker / uc_crc32sum.elf  ← ★★★ CRC32，可伪造，不是签名
```

**★ 而且 `crypter.elf` 只有两个明文 C 标识符：**
```
Dec_ScrambleInit  (0x1d7f)
Fsys_Init         (0x1e12, 0x67eb)
```
**"A replacement library can export both and whichever is looked up resolves."**

**★★★ 意义：存在一条"伪造更新包"的代码执行路径（只校验 CRC）。
⚠️ 但那是**写 flash**，风险远高于内存 bind —— 不作为首选。**

---

## 五、§11 他们验证过的改动

```
/usr/share/pmbp/DeviceInfo.xml        版本号 1.3.00.19200 → 9.9.99.99999
/usr/share/app/string_english_f.uxc   "Playback" → "OPENCODE"（24 字节）
/usr/lib/libtestcmd.so                66 字节代码执行验证，已回退
```

**★ 但他们的注释：**
> "The first two are md5-verified on the camera and **never seen on screen** —
> the application that would draw them is not deployed."

**⇒ 改了字串但屏幕上看不到，因为绘制它的应用没部署。**

---

## 六、⚠️ 另一处需要核实的说法

**§13 说：`/usr/tool/` is on "the persistent writable `/usr`"**

**★ 但我们实测 `/usr` 是 `ext2 ro`（只读挂载）。**

**⇒ 需要核实 `/usr/tool` 是否单独挂载，或他们的说法有误。**

---

## 七、`.uxc` 容器格式（已完整解出，换资源需要）

```
0x00  3 bytes  magic 'uxc' | 'uxb' | 'uxa'
0x03  u8       format version (7)
0x08  u16      stream version: 8 for .uxc, 9 for .uxb/.uxa
0x0A  u16      resource_id（不是校验和）
0x0C  u16      control = 最大 id + 1
0x0E  u16[]    sparse id→slot 索引，0xffff = 缺失

index_count = control & 0xff
data_start  = align4(0x10 + 2*index_count)
```

**验证：302/302 文件、12,082/12,082 索引项全部有效。**

### color_cmn.uxc
```
0x58..0x137  28 条记录 × 8 字节
  +0 u16 id (0x4000..0x4022)  +2 u16 const 0x3a09
  +4 u8 r  +5 g  +6 b  +7 a
★ 无校验和
★ 硬件验证：0x400c 改成品红 ⇒ 实时取景框线变品红
```

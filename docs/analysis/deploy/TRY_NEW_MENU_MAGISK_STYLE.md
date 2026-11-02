# 怎么试新菜单 —— Magisk 式方案

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


> 回答：「我们如果想试试新菜单的话，怎么试试，以及怎么恢复，有没有类似 magisk 这种的」

---

## 一、Magisk 类比：有等价物，但不是 overlayfs

| Magisk | 本机对应 | 状态 |
|---|---|---|
| **overlayfs 覆盖 `/system`** | ❌ **内核无 overlayfs**（已查证 `vmlinux` 无 overlay 字样）| **不可用** |
| **`mount --bind` 覆盖文件** | ✅ **可用** | **我们的方案** |
| **重启即恢复** | ✅ 同 | ✓ |
| **Manager 开关** | `safemode` 文件 + bind 清单 | ✓ |
| **模块目录** | `/log/zv1f_ui/` | ✓ |
| **开机自动挂载** | ❌ **本机无启动钩子** | **做不到** |

**★ 关键差别：Magisk 开机自动挂载模块；本机没有可 hook 的启动点
（`/etc` 里无 `profile`/`init.d`/`rc.local`/`inittab`，`/` 与 `/usr` 只读），
**⇒ 每次开机要手动触发（或靠已驻留的 agent）。**

---

## 二、重要的前置事实：新菜单引擎 ZV-E10 已经有了

```
ZV-E10 /usr/lib/:
  viewUnified2.so  14,284,732 B   ← 含 global.xdb 引用 + PAS_Menu* 新菜单控件
  viewUnified3..8.so             ← 另外 6 个
```

**★ `viewUnified2.so` 里有 `PAS_MenuSelectBeltZako`、`CmnSettingNodeUtil::initBeltWidget`
这类**新菜单 UI 控件** ⇒ 控件层不缺。**

**缺的是**应用框架**：**

| | ZV-E10（老菜单）| ZV-1F（新菜单）|
|---|---|---|
| 应用 | `im.elf`（23 KB 启动器）+ **单体 `libObj.so` 21 MB** | `appFw.so` 53 MB + `libObj.so` **3.9 MB** |
| 库总数 | 202 | 143 |

**⇒ 「试新菜单」本质是：**把 ZV-1F 的 appFw 那套跑在 ZV-E10 上**。**

---

## 三、怎么试（四步，全部可回滚）

### Step 0：确认移植包

**已有素材：`_archive/dirs/zve10_portable/zv1f_ui/`（132 MB）**
```
bin/im.elf                     23 KB
lib/appFw.so                   53 MB
lib/libObj.so                 3.9 MB
lib/libInfraSKD.so            7.2 MB
lib/AdjChkConfig.so            82 KB
lib/libGOConnectionManager.so  72 KB
share/app/global.xdb          13 KB
share/app/LayoutMaster_Menu.uxc 3.6 KB      ← 菜单布局
share/app/image*.uxc          ~60 MB        ← 图像资源
```

### Step 1：上传到相机 `/log/zv1f_ui/`

**⚠️ PUSH 限制：单次 ≤ **16 MB**，且会**截断**目标文件（`O_TRUNC`）**

```
⇒ 大文件必须切片：
   本地: split -b 15M appFw.so part_
   每个 part: cam.py push /log/zv1f_ui/part_NN <part>
   相机: cat part_* > appFw.so ; rm part_*
```

**★ 好消息：PUSH 会自动备份目标为 `.orig`（`cp -a path path.orig`）—— 它自带一层保护。**

**★ `/log` 是 500 MB 分区，132 MB 放得下。**

### Step 2：写 bind 清单

```
# /log/zv1f_ui/bind.txt，每行：源<TAB>目标
/log/zv1f_ui/lib/appFw.so	/usr/lib/appFw.so
/log/zv1f_ui/lib/libObj.so	/usr/lib/libObj.so
/log/zv1f_ui/share/app/global.xdb	/usr/share/app/global.xdb
/log/zv1f_ui/share/app/LayoutMaster_Menu.uxc	/usr/share/app/LayoutMaster_Menu.uxc
...
```

### Step 3：预检 + 武装

```
① 部署 payload_guard（⚠️ 写 .so，需用户同意 —— 规矩 0）
   或：手工检查每个源文件存在、非空、ELF 架构正确
② guard 通过 → 写 bind.txt
③ agent_v14 的 apply_binds(1) 执行 mount --bind
④ agent 的 watchdog 开始 90 秒倒计时
```

### Step 4：重启 UI 并观察

```
RUN: kill 掉 im.elf / appFw 进程，让它重启
或：直接 reboot（但那会清掉 bind，所以不能 reboot）
```

**★ 只能 kill UI 进程让它自己重启，不能 reboot。**

---

## 四、怎么恢复（三档，从轻到重）

| 档 | 手段 | 速度 | 前提 |
|---|---|---|---|
| **1** | **`umount` 所有 bind** | 立即 | agent 还活着 |
| **2** | **等 90 秒 —— 看门狗自动 umount + reboot** | 90 秒 | bind.txt 存在 |
| **3** | **断电重启** | 10 秒 | **永远有效**（bind 不落盘）|
| **4** | **放 `safemode` 文件** | — | 防止下次再武装 |

**★★★★ 第 3 档是最后保障：`mount --bind` 只在内存里，断电必然回到原厂。**

**恢复命令（档 1）：**
```bash
cam.py run "/bin/busybox cat /log/zv1f_ui/bind.txt | /bin/busybox while read s t; do /bin/busybox umount \$t; done"
```

---

## 五、⚠️ 三个必须知道的坑

### 坑 1：`/etc/ld.so.preload` —— 当初变砖的元凶

**旧方案（见 `README_WINDOWS.md`）写的是：**
```
/etc/ld.so.preload → /etc/zve10_agent.so
```
**⇒ 这是**持久**配置，在任何可恢复代码之前加载。库一坏，`init` 都起不来。**

**⇒ 现在**绝对不碰**。**

### 坑 2：`mount -o remount,rw /usr` —— **比变砖更危险**

**那份 README 的示例里就写着 `mount -o remount,rw /usr`。**
**★★★ 千万别做：**

```
/usr = /dev/nflasha15，LZP 压缩分区
写路径 = uz_emmc_do_compress()，只有 20 字节，直接 return 0（空桩）

⇒ remount rw 可能"成功"，但写入要么被丢弃、要么把未压缩数据
   写进物理分区 ⇒ 损坏 LZP 镜像 ⇒ 重启后 /usr 解压失败 ⇒ 起不来
```

**★ 而 `/usr` 的损坏**重启不会恢复**（它已经在 flash 上了）。**

**⇒ 结论：只用 `mount --bind` 覆盖**视图**，绝不 remount /usr rw。**

### 坑 3：ABI 不匹配

**ZV-1F 的 `appFw.so` 依赖 ZV-1F 的其它库（`libInfraSKD.so` 等）和硬件抽象。
直接换上去很可能**直接崩或在初始化时挂住**。**

**⇒ 首次尝试应**只换一个最小文件**（比如只换 `global.xdb`），确认机制通了再逐步扩大。**

---

## 六、我的建议顺序（保守试错）

```
① 只 bind global.xdb（13 KB 资源索引）
   ⇒ 验证 bind 机制 + watchdog 正常工作
   ⇒ 风险最低：坏了大不了 UI 显示异常，重启就回

② 只 bind LayoutMaster_Menu.uxc（3.6 KB 菜单布局）
   ⇒ 看菜单结构有没有变化

③ 一起 bind global.xdb + LayoutMaster_*
   ⇒ 看是否出现新菜单布局

④ 最后才考虑 bind appFw.so + libObj.so
   ⇒ 这一步风险最高（ABI）
```

**★ 每一步之间都 reboot 一次（清掉 bind），确保从干净状态开始。**

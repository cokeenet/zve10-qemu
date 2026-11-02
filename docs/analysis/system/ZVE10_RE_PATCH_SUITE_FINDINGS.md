# `cokeenet/zv-e10-re` 里对我们有用的结论

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


> 来源：`https://github.com/cokeenet/zv-e10-re`（508 MB，ZV-E10 专用逆向 + 补丁套件）
> 本文只摘录与本项目（QEMU 老菜单 / 自制固件）直接相关的部分。

## 一、★★★ `/usr` 写入为什么重启后消失 —— LZP 压缩块设备

**`/usr` 不是从 `/dev/nflasha15` 提供，而是 `/dev/dnflasha15`（Major 252, Minor 143）。**

**`dn` = decompressed-nflash**：Sony 的 eMMC 驱动（`emmc.ko`）里有一个硬件
**LZP（Lempel–Ziv Partition）解压器**，读时把物理 flash 块解压到 RAM。

**而写路径是空壳：**
```c
// uz_emmc_do_compress() — emmc.ko 偏移 0x5cf8
mov r0, #0        // 直接返回 0 —— no-op
ldm sp, {fp, sp, pc}
```

**错误串佐证：**
```
Error : Unsupported write on compressed partition...[%d]
Error : Can not erase the decompressed partition %s
```

**⇒ 推论：**
1. **`/usr` 的任何改动**不会持久** —— 没有压缩器把改动写回去**
2. **⇒ 改固件必须走 updater / SD 卡路径，不能靠改 `/usr` 落盘**
3. **⇒ QEMU 里 `/usr` 是内存中的解压副本，改动天然不持久**

## 二、★★★ 显示面：真机**没有 CPU 可见的帧缓存**

> "There is no CPU-visible framebuffer. The display is descriptor-driven
> (APL/AIC + XDMAC + HME) and the panel is fed by a private `ldec` driver."

**实测的四个 `/dev/stream` 面：**

| 面 | 偏移 | 特征 |
|---|---|---|
| **s2** | **`0x20312000`** | **98.7% 为零，稀疏元数据** |
| s3 | `0x3F7ED000` | 2.8% 零，全 256 值 —— `wbi_cmpr` 压缩器 |
| s4 | `0x228B2000` | 89.3% 零，打包 sub-byte nibbles |
| s1 | `0x22312000` | 98.7% 零 |

**关键实验：向 s4 写 `0xFF` 后重读，99.8% 字节相同（4089/4096，7 个差异是活堆指针）
⇒ UI 会重绘它 ⇒ 是后台/暂存缓冲，不是面板扫描源。**

**⚠️ 与我们的做法对照：QEMU 侧把 `0x20312000` 当显示缓冲镜像 —— 这是**QEMU 侧的选择**，
不是真机语义。它对我们有效（streamfb 镜像它），但不能据此推断真机行为。**

**⇒ 意义：App 写 `0x20312000` 证明**绘制代码跑了**，但那不是真机显示路径。**

## 三、★ `/dev/mem` 只能按页读

> `dd bs=4096 count=1` 返回 4096 字节；`dd bs=4 count=4` 返回**0 字节文件且无报错**。

**⇒ 亚页读写会静默地什么都拿不到 ⇒ 靠 `/dev/mem` 精细追指针不可行。**

## 四、★ `viewUnified2.so` 就是 UI 引擎

| 二进制 | 字节 | 说明 |
|---|---:|---|
| **`viewUnified2.so`** | **14,284,732** | 最大非位图 ELF；**含字面量 `global.xdb`** |
| viewUnified3 | 941,232 | |
| viewUnified4 | 3,287,604 | |
| viewUnified5 | 749,136 | |
| viewUnified6 | 589,336 | 调色板 id 富集 15.4× |
| viewUnified7 | 579,944 | |
| viewUnified8 | 283,208 | |

**⇒ 七个引擎，大概一屏类一个。`viewUnified2` 是会加载资源文件的那个。**

**旁证：**
- **`viewUnified2.so` 里 `global.xdb` 恰好出现一次** —— 加载器点名它要打开的文件
- **`libObj.so` 里 `uxc` 出现两次** —— 容器魔数在那边也被识别
- **`libSysDef.so` 调色板 id 富集 45.7×**（u16 4 对齐：`0x4000` ×1095、`0x4004` ×27…）—— 默认配色所在

**⇒ 菜单资源来自 `global.xdb`；默认配色在 `libSysDef.so`。**

## 五、★ VDF 不是通往可见效果的路线

> 36 个 VDF `Execute`/`Activate` 方法里 35 个能按名字解析，但
> **每个显示命令都吃两个活接口对象，并经子系统 vtable 槽 `0x44c`–`0x858` 才到面板。**
> **从注入代码调用它是挂起风险，不是可见效果。**

## 六、★ 可复用的工具（无交叉编译器时的招法）

**用 `keystone` + `capstone` 汇编静态 ARM Linux 助手，手工构建 ELF32：**

| 工具 | 作用 |
|---|---|
| `arm_helper.py` | 构建助手 |
| `arm_verify.py` | ELF + 反汇编检查（mode-aware）|
| `arm_static_check.py` | 抓寄存器/分支错误 |
| `arm_stack_sim.py` | **模拟每次栈访问与帧大小对比** |

**⚠️ 关键陷阱：**
1. **`p_offset` 与 `p_vaddr` 必须**模页同余**，否则内核映射错页、进程被 SIGKILL**
2. **未对齐的 `p_offset` 能让文件保持 ~1 KB —— 这很重要，因为相机 shell 会把命令截断在 ~1022 字符，超长会卡死会话**
3. **两次段错误都是栈溢出，而反汇编看起来正常 ⇒ 栈模拟是必要检查**

## 七、★ 关于 `bootin.elf` 清 `LD_PRELOAD`

**该仓库文档称：`/usr/bin/bootin.elf` 在启动 `im.elf` 前用 `putenv("LD_PRELOAD")` 清除它，
因此经 `/setting/mode/preload` 预加载跨重启失效。**

**本项目静态核实：**
- `bootin.elf` 里确有裸串 `LD_PRELOAD`（va `0xA778`）与 `LD_PRELOAD=`（va `0xA6DC`）
- 共 7 处 `putenv` 调用；其中 `0xA032..0xA03E` 三处连续的是 **LeakTracer 环境**
  （`LT_CHECK_PROCESS=1` / `LT_INT_DIR=/tmp/lt` / `LT_MISC_DIR=/tmp/lt`），**不是清 preload**

**⇒ "清 preload" 的结论方向可信（裸串存在），但具体调用点尚未定位。
不过本项目 QEMU 侧的 LD_PRELOAD 型 shim 实际是生效的，说明注入点不依赖该环境变量继承。**

# 2026-10-27 晚 — 关键纠错与新通道（务必先读）

## 一、★ 推翻了三个错误结论（都源于同一个错误的判据）

之前我用 `s_mnt_count`（ext2 超块挂载计数）判断"是否挂载"，三次读到不变就断言"没挂上"。

**实测 `/proc/mounts`（通过 agent 读取）证明这个判据不成立：**
```
/dev/nflasha15 /usr ext2 ro,relatime,errors=continue 0 0
                              ↑↑ 只读挂载 ⇒ 超块写不进去 ⇒ s_mnt_count 永远不变
```

⇒ 因此以下三个结论全部作废：

| 原结论 | 正确结论 |
|---|---|
| "300 MB / 38 组的 fs 挂不上" | ❌ 错。**/usr 一直都能挂载** |
| "250 MB / 32 组的 fs 挂不上" | ❌ 错 |
| "原厂底子 + M2 内容挂不上" | ❌ 错 |

⇒ **真正的失败一直在应用层，不在挂载。** 之前所有"改 inode_size / 改几何 / 换 fs"的努力都没必要。

## 二、★ `/usr` 可以 remount 成可写（推翻 SAFETY_RULES 规矩 2）

```
mount -o remount,rw /usr   → rc=0
/proc/mounts: /dev/nflasha15 /usr ext2 rw,relatime,errors=continue
touch /usr/_test           → WRITE OK
```

⇒ SAFETY_RULES.md 里"「改 /usr 再重启恢复」这条路不存在"**不成立**。

⚠️ **但有一个陷阱（本次踩了）：**
```
被运行中进程映射的文件，unlink/覆盖后【空间不会立即释放】
⇒ 在 im.elf 运行中删库腾空间 = 白删（/usr 仍只剩 1 个空闲块）
⇒ 必须先重启（映射释放）再操作
⇒ 而且 cp 大文件时 O_TRUNC 会先把目标截成 0，写不进去就留下半截文件
   本次就因此把 /usr/lib/libObj.so 弄成 13 MB / 21.3 MB
```

## 三、★ 持久调试通道已经建立（不需要 Pico / USB / 拔插）

```
/setting/mode/dmode   = "3e"
/setting/mode/preload = /usr/tool/libefence.so:/log/agent_v14.so:/log/autonet.so

bootin 的白名单只做前缀比较（4 个调试库路径），libefence.so 在 E10 上不存在
⇒ 通过校验后被 ld.so 警告忽略；后面两项才是真载荷
glibc 2.11.2 支持冒号分隔、单条失败不致命（已从 ld-2.11.2.so 取证）
```

- **agent_v14.so**（/log，18,104 B）：TCP 7777，RUN/PULL/PUSH
  - 注意 PUSH 是 `malloc(n+1)` 整块接收 ⇒ **单次不要超过 4 MB**（128 MB 内存只剩 19 MB 空闲）
  - PULL 是静态 8 MB 缓冲
  - 分块推 + 相机上 `cat` 拼接可用，实测 2.3 MB/s
- **autonet_wifi.so**（/log，12,532 B）：开机自动连 WiFi
  - 开关 `/setting/mode/zve10_net` = "on" 或 `/log/zve10_net.on`
  - 内置 SSID=CokeeHotspot / PSK=cokeesuki / 静态 10.42.0.158 / GW 10.42.0.1
  - 等 40 秒，若 wlan0 已有 IP 则跳过；否则自己起 wpa_supplicant
- **兜底通道**（不依赖 WiFi）：Jiritsu USB 写 `/tmp/jiritsu/tmmc.jit` → agent 执行 → 读 `/tmp/jiritsu/DIAC.csv`

## 四、★ 崩溃 / 自救能力边界（实测）

| 场景 | agent 还能起来吗 |
|---|---|
| libObj / appFw / 资源坏了 | **能**（preload 在 im.elf 进程启动时加载，早于 dlopen） |
| im.elf 本身坏了 | **不能**（bootin exec 的是 /usr/bin/im.elf） |
| /usr 挂不上 | **不能** |
| app 挂了但 init 活着 | **能**（`wpa_supplicant` 由 init 启动，PPID=1，WiFi 独立存活） |

⇒ **安全原则：可以改 /usr/lib 和 /usr/share，不要碰 /usr/bin/im.elf。**

## 五、★ 移植的硬技术结论

### 5.1 入口链（已用 nm 逐级确认）
```
M2 的 libIMDB.so 注册 "appFwBoot.so"
  → appFwBoot.so 导出 Application_System_Init   ✓
      → 内部 Main() dlopen "appFw.so"
          → appFw.so 导出 Application_Start      ✓
E10 老架构: libObj.so(21 MB) 导出 Application_System_Init，无 appFw.so
```

### 5.2 符号闭包（这是移植可行性的硬指标）
```
appFw.so 有 454 个未定义符号
用"只换 5 个文件"（appFw/appFwBoot/AdjChkConfig/libObj/libIMDB）：仍缺 33 个
      缺的几乎全是 mpr::* / sidb::* / DefInfraCCPFStatic / Fsys_FormatV2 / GCM_
      ⇒ E10 的 libmpr.so(12.4 MB) 不提供这些，必须用 M2 的 libmpr.so(3.0 MB)
用 M2 完整 lib+share（n15_final.ext2）：仍缺 4 个
      _Jv_RegisterClasses / __gmon_start__  ← CRT 弱符号
      infra_wrapper_bb / imaging 的 2 个     ← ABI 名字截断
      ⇒ 【闭包成立】
```

### 5.3 资源层必须一起换
```
style_cmn.uxc   E10 1888 B schema 3  →  M2 2004 B schema 4   ★不换就解析失败
global.xdb      E10 12,252           →  M2 16,112           ★
image_cmn_43.uxc E10 27 MB           →  M2 无（改用新命名）
share 整体: E10 80.5 MB → M2 82.1 MB，净 +1.6 MB（几乎不占空间）
```

### 5.4 内核不用换
```
ZVE10 / ZV1F / ZV1M2 同为 Linux 3.0.27_nl-rt106+ / gcc 4.5.1 / ARMv7
ZV1F 与 ZV1M2 的 vmlinux 99.99% 相同（只差 8 个 64B 块）
ZVE10 与它们差 30.5%（E10 独有硬件：APS-C 传感器、可换镜头卡口、热靴…）
ZV-E10 【没有 EVF】
ZVE10M2 是 4.9.337 aarch64，完全不同平台
kmod: 103 个，49 个与 M2 不同（机型硬件相关）⇒ 必须用 E10 的
vermagic 完全一致 ⇒ 模块能正常 insmod
```

### 5.5 已知硬缺失
```
libInfraLensCommunicator.so  只在 E10（8,196 B），ZV1F/ZV1M2 都没有
  ⇒ 新菜单默认没有可换镜头通信能力；已把它加进镜像（M2 的 libIMDB 也引用该名 3 次）
```

## 六、★ 当前设备状态（用户吃饭时）

```
USB    054c:0994 = 充电/关机模式
WiFi   不通（ping 10.42.0.158 失败）
/usr   libObj.so 只剩 12,996,608 B（应 21,305,528）— 半截
       libIMDB.so 37,204 B ✓ 已正确恢复
       老 UI 的 10 个 .so 已删除（备份在 /log/zvegisk/ui_backup/，md5 已核对）
       appFw 等 5 个文件【未装】，但已在 /log/zvegisk/stage/ 且 md5 已核对
/setting  preload 三项 / dmode="3e" / zve10_net="on"
/log     stage/ 5 文件 + usr_backup/ + ui_backup/ + autonet.so + agent_v14.so
```

⚠️ **用户反馈"除了大容量之外全部是好的"** ⇒ 说明 im.elf 能起、WiFi 能通、UI 可能半可用，
但 **USB 大容量模式没起来** ⇒ 依赖大容量模式的 Jiritsu 通道可能用不了 ⇒ **这次需要 Pico**。

## 七、★ 下一步（唯一推荐路径）

**刷 `n15_final.ext2`（Pico + fastboot，300 MB ≈ 100 s）**

它已经过全套预检：
```
M2 完整 lib+share（符号闭包只差 4 个弱符号）
E10 的 im.elf / bootin.elf / sen.elf / kmod 103 个
E10 的 libInfraLensCommunicator.so
300 MB（resize2fs 扩容自原厂 fs，UUID 与几何参数保留原厂）
818 文件 / 231 MB / 剩 59 MB
```

刷完后：
1. `/setting/mode/preload` 未变 ⇒ **agent + autonet 自动启动** ⇒ 无论新菜单成不成都有通道
2. 若出现新菜单 ⇒ 移植成立，进入功能验证
3. 若没有 ⇒ 通过 agent 读 `/proc/<im.elf>/maps`、`/proc/<im.elf>/fd`、`/log`、`dmesg`，定位卡在哪一步

**备选/回滚**：`/root/fw/zve10_backup/nflasha15.img`（原厂 /usr 完整备份，全量校验过）

## 八、不要做的事（本次踩坑总结）

1. ❌ 不要用 `s_mnt_count` 判断 ro 挂载
2. ❌ 不要在 im.elf 运行时删库"腾空间"——块不会释放
3. ❌ 不要用 `cp` 直接覆盖大文件——O_TRUNC 会留下半截文件且原文件丢失
4. ❌ 不要单次 PUSH 超过 4 MB（128 MB 内存）
5. ❌ 不要只换 5 个 lib —— mpr::/sidb:: 符号缺口会让 appFw 调不通
6. ❌ 不要忘记换 `/usr/share/app` —— style_cmn 是 schema 4
7. ❌ 不要碰 `im.elf`（agent 靠它加载）

---

# 收工后的静态分析（2026-10-28 00:05）

> ## ⛔ 本节结论已作废（2026-11-02）
>
> 下面这次实验**方法有误**，结论**作废**，不再作为任何推断依据。
> 上游原作者已在 [issue #4](https://github.com/cokeenet/zve10-qemu/issues/4) 澄清：
> `/dev/nflasha15` 是**普通未压缩 ext2**，`mount -o remount,rw /usr` 之后的写入**重启后保留**。
> 「覆盖被丢弃 / 新建保留」这个观察是我们操作上出了问题，不是文件系统行为。
> 原文保留仅供追溯。

## ★ 一、`/usr` 的运行时写入【不持久】—— Pico 是硬约束

```
实验: 通过 agent remount rw /usr → 写 bootin.elf 为启动器脚本(291B) → 备份 bootin.real → sync → 重启
结果: 重启后 bootin.elf 变回 15,336 B 原厂 ELF（md5 d8bfb4f7）
      但新建的 bootin.real 仍然存在
      /tmp/BOOTIN_REACHED 不存在（启动器从未执行）

⇒ 覆盖已有文件的写入被丢弃，新建文件能留下
⇒ SAFETY_RULES.md 那句"写路径是空桩"反过来也成立: 改了也不生效
⇒ 结论: /usr 的任何改动只能走 fastboot 裸块写
⇒ 但可以稀疏写: 只改 1 个文件 = 5 个 64KiB 块 = 0.3 MB，1 秒
```

## ★ 二、分层审计结果：hybrid 的完整度等同于 M2 原厂

```
M2 的 libIMDB 表: .data.rel.ro @0xf00c, record base 0xf090, 228 条 × 44 B
  profile 位: default 0x01 / qemu 0x06 / nfs 0x08 / set 0x10 / USBJ 0x20 / RE-SUB 0x40
  default(0x01) 要装载 131 个模块

对照 hybrid(nflasha15_v5) vs M2 原厂 /usr:
  hybrid 缺 81 个
  M2 自己也缺 84 个
  ★ hybrid 比 M2 多缺的 = 0 个

⇒ 我们的分层（lib+share 取 M2、其余取 E10）没有引入任何额外缺口
⇒ 失败原因不在"缺文件"
```

## ★ 三、入口模块只差一个名字

```
E10 libIMDB: idx=140  enable=0x01  appFw.so      + Application_System_Init   （文件不存在，容忍）
M2  libIMDB: idx=145  enable=0x01  appFwBoot.so  + Application_System_Init   ← 名字不同
             idx=145  同一记录还引用 AdjChkConfig.so / AppConfig.so（后者两边都没有）
             idx=148  enable=0x01  gui.so + DPro_App_UI_init（文件两边都没有）
⇒ 换 M2 的 libIMDB 后，E10 的 im.elf 会去 dlopen appFwBoot.so 并 dlsym Application_System_Init
```

## ★ 四、`sabin`（DSP 固件）属平台层，保留 E10 是对的

```
两版 sabin 差异: 23 个同名不同内容（dcv_psf_table_still 57848→525968 等）+ 仅E10 5个 + 仅M2 14个
但: 没有任何用户态库/二进制按路径或裸名引用 sa_*.bin
⇒ DSP 固件由 CP/RTOS（av-cam.bin）侧加载，必须与 /system 的 CP 固件配套
⇒ 保留 E10 的 sabin 正确 ✓
```

## ★ 五、下一步静态分析（不用动相机）

```
最可疑的剩余点: E10 的 im.elf 与 M2 的 appFwBoot.so 之间
  Application_System_Init 的【签名/行为】是否一致

  E10: libObj.so 导出 Application_System_Init @0x8a2408（21 MB 单体引擎）
  M2 : appFwBoot.so 导出 Application_System_Init @0x1270（8 KB 桥）
  ⇒ 若参数含义不同（例如 E10 版期望某个结构体指针，M2 版按无参处理）
    ⇒ 调用即崩或返回后无屏 ⇒ 完全符合"无 UI、无日志"的现象

查法（纯静态）:
  1. 各自反汇编该函数前 40 条指令，看它读了哪些入参寄存器、写了什么全局
  2. 看 im.elf 调用点（0xaf14 附近的 IMDB 装载流程）传什么参数
  3. 对比两版对 R0-R3 的用法
```

## 六、本轮全部结论汇总（一句话版）

```
/usr 能挂载；不能运行时改；M2 应用层文件足够；坑在运行时 ABI/协议层。
下一步只做静态分析：对比 Application_System_Init 的调用约定。
```

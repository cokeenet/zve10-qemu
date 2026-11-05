# `.uxc` 格式解析 & ZV-1M2 vs ZVE10 一代 菜单差异

日期：2026-11-05 · 数据来源：`/mnt/build/fw/COMPARE/extracted/{ZV1M2,ZVE10,ZVE10M2,ZV1F}/usr/share/app/`

---

## 一、硬件谱系（来自 OpenMemories hardware 文档）

| SoC | 代号 | 年代 | 代表机型 |
|---|---|---|---|
| CXD90014 | Musashi/Kojiro | 2013-10 | ILCE-7, 6000, 7M2, 6500 |
| **CXD90045** | **Astra** | 2017-04 | ILCE-9, 7M3, RX100M6, **6600** |
| CXD90057 | Mira | 2020-07 | ILCE-7SM3（BIONZ XR） |

> 文档原文：“CXD90014 和 CXD90045 被称为 BIONZ X，CXD90057 被称为 BIONZ XR。”

**⇒ ZVE10 一代 与 ZV-1M2 同为 Astra / BIONZ X 家族。
ZV-E10 II (ZVE10M2) 反而用了新图形后端 IPSX —— 它才是异类。**

---

## 二、`usr/share/app` 数据对比

| 固件 | `.uxc` 数 | 总文件 | LayoutMaster_Menu | global.xdb | master_camera |
|---|---|---|---|---|---|
| ZVE10 一代 | 299 | 308 | 3512 | 12252 | 101804 |
| **ZV1M2** | 220 | 232 | **3636** | **16112** | **101868** |
| **ZVE10M2** | 220 | 232 | **3636** | **16112** | **101868** |
| ZV1F | 152 | 164 | 3636 | 13320 | 101868 |

**ZV1M2 与 ZVE10M2 的 `LayoutMaster_Menu.uxc` / `master_camera.uxc` 逐字节相同（MD5 一致）。**
两者差异只在 206 个 `string_*.uxc`（ZV1M2 每个多 1.5~2.5 KB）和 `global.xdb`。

### 图形后端标识（决定移植可行性）

```
ZV1M2   : image_00_34_ASTRA.uxc ×6   ← ASTRA = ZVE10 一代的图形系统
ZVE10M2 : image_00_23_IPSX.uxc  ×6   ← IPSX  = Ⅱ代新后端
ZVE10   : image_cmn_43.uxc 等旧命名（无 image_XX_XX_*）
```

---

## 三、`.uxc` 文件格式（已解析）

```
偏移 0    4 B   magic = 'uxc' + 版本字节（实测 0x07）
偏移 4    4 B   保留（0）
偏移 8    2 B   版本号 = 8
偏移 10   2 B   字段A（LayoutMaster_Menu=143，master_camera=78）
偏移 12   2 B   条目数 N（LayoutMaster_Menu=15，master_camera=335）
偏移 14   2 B   字段B（LayoutMaster_Menu=1，master_camera=21）
偏移 16   N×2 B 偏移表，u16 LE；0xFFFF = 空槽
         表结束 = 16 + N*2
偏移 16+N*2 起：各条目结构体，按偏移表索引

实测 master_camera（N=335）：
  有效条目 294 个，空槽 41 个
  条目结构体头部含 4 字节哈希（如 0x7df1075f / 0xc34c39a7 / 0x1b572204）
```

**这解释了 appFw 里 `0x54e140` / `0x54e108` 两个函数：
它们就是 `[全局表 + 索引*4]` 的查表访问器。**

---

## 四、★ `master_camera.uxc` 逐项差异（移植清单）

**335 项中 330 项完全相同，只有 5 项不同：**

| idx | ZV1M2 | ZVE10 | 差 | 含义 |
|---|---|---|---|---|
| **52** | 34 B | 61 B | **-27** | ZV1M2 少一个子项（27 B） |
| **92** | 162 B | 145 B | **+17** | ZV1M2 多 17 B |
| **254** | 176 B | 158 B | **+18** | ZV1M2 多 18 B |
| **280** | 91 B | 83 B | **+8** | ZV1M2 多 8 B |
| **334** | 76632 B | 76584 B | **+48** | 尾部数据段多 48 B |

### idx 52 的差异（唯一"退化"项）
```
ZV1M2  34 B = ZVE10 61 B 的严格前缀
ZVE10 多出 27 B：
  80 55 02 08 00 00 69 00 08 00 00 41 00 1b 57 22 04 01 0e 08 00 00 97 01 08 00 00
```

### idx 92 / 254 / 280 的差异
均为 ZV1M2 **新增/扩展**了条目内容，独有段含新的哈希与 ID：
```
idx 92 : 7f 01 90 0f 00 1f 02 80 55 02 08 00 00 c4 00 ... 
idx 254: a7 01 11 01 1f 0b 9b 40 01 90 02 00 7d f1 07 5f f7 46 b8 c4 ...
idx 280: f4 d3 4e 00 00 00 00 00 04 1f 02 80 55 02 08 00 00 40 00 ...
```

---

## 五、结论：移植工作量的真实规模

```
✅ 菜单框架同构：335 项中 330 项（98.5%）完全一致
✅ 图形后端同代：ZV-1M2 用 ASTRA = ZVE10 一代同款
✅ 内核/glibc 相同：vermagic 3.0.27_nl-rt106+ / glibc 2.11

需要移植的只有：
  1. master_camera.uxc          → 5 个条目（约 118 字节的结构差异）
  2. LayoutMaster_Menu.uxc      → 3636 vs 3512（差 124 B）
  3. global.xdb                 → 16112 vs 12252（差 3860 B）
  4. image_XX_XX_ASTRA.uxc ×6   → 约 51 MB 图像资源（ZVE10 无同名文件）
  5. 206 个 string_*.uxc        → 字符串表（每个多 1.5~2.5 KB）

⇒ 真正的工作量在 4（图像资源，51 MB）和 5（字符串表）。
   菜单结构本身（1、2、3）差异极小。
```

---

## 六、下一步

```
A. 解析 master_camera.uxc 的条目结构体字段含义
   （把 idx 52/92/254/280 的差异翻译成"哪个菜单项改了"）
B. 对比 LayoutMaster_Menu.uxc（3636 vs 3512，差 124 B）
C. 检查 ZVE10 的 libObj 是否能识别 *ASTRA.uxc（图形后端能力确认）
D. 评估 image_XX_XX_ASTRA.uxc 的格式是否与 ZVE10 的 image_cmn_43.uxc 兼容
```

---

## 七、`string_english.uxc` 文案差异（用户可见的菜单变化）

| | 可读文案数 | 文件大小 |
|---|---|---|
| **ZV1M2** | 662 | 46552 B |
| **ZVE10 一代** | 2313 | 174104 B |
| 共有 | 625 | |
| ZV1M2 独有 | 37 | |
| ZVE10 独有 | 1688 | |

> ⚠️ ZV1M2 的 string_english.uxc 反而比 ZVE10 **小 3.7 倍**（46 KB vs 174 KB）。
> 说明索尼把大量文案从 string 表移到了别处（推测 `global.xdb` 16112 vs 12252）。

### ZV1M2 独有的 37 条文案（= 新菜单新增的能力）

```
 1. Can not connect to the server via Wi-Fi
 2. Can't connect since airplane mode is "On".
 3. Can't recognize LAN cable: Reconnecting
 4. Can't transfer to FTP server: Reconnecting
 5. Cannot find access point: Reconnecting...
 6. Cannot set during USB LAN/tethering connection.
 7. Cannot set during tethering connection.
 8. Compensates for darkening of the edges of the screen as a characteristic of the lens.
 9. Compensates for distortion of the screen as a characteristic of the lens.
10. Could not create directory on FTP server
11. Error has occurred on the Wired LAN.
12. Locks focus when the focus hold button is pressed.
13. Necessary use a memory card
14. Proxy Recording to"Off".
15. Rec. Media Card to "Slot1" or
16. Rec. Media Card to "Slot1".
17. Reduces color registration errors at the edges of the screen as a characteristic of the lens.
18. SDXC U3/V30 or change the
19. SDXC V60 or change the
20. SDXC V90 or change the
21. Select media card.
22. Sets to a speed that can shoot a maximum of 120 shots in 1 second.
23. Sets to a speed that can shoot a maximum of 30 shots in 1 second.
24. Sets to a speed that can shoot a maximum of 60 shots in 1 second.
25. Switch to a memory card
26. Switch to a memory card higher than
27. Unable to perform Clip Flag
28. Unexpected error has occurred on Wi-Fi.
29. You may be able to rec by set
30. You may be able to record by setting
31. higher than CFexpress VPG200.
32. higher than CFexpress VPG400.
33. higher than SDXC U3/V30 or
34. higher than SDXC V60 or
35. higher than SDXC V90 or
36. necessary use a memory card
37. on this image.
```

### 分类归纳

| 类别 | 条目 |
|---|---|
| **镜头补偿（新）** | #8 暗角补偿 / #9 畸变补偿 / #17 色差补偿 |
| **连拍速度新档** | #22 120fps / #23 30fps / #24 60fps |
| **存储卡规格要求** | #18-20 SDXC U3/V30、V60、V90；#31-32 CFexpress VPG200/VPG400 |
| **网络/FTP** | #1-7、#10-11、#28 |
| **对焦保持按钮** | #12 Locks focus when the focus hold button is pressed |
| **Clip Flag** | #27 Unable to perform Clip Flag |

> 注意：**ZVE10 一代独有的 1688 条**里包含大量它的原生功能（闪光灯/AF模式/PAL等），
> 说明两代的产品定位不同（ZVE10 是 APS-C vlog 机，ZV-1M2 是 1 英寸紧凑机）。
> **移植时不能简单地"用 ZV1M2 的文案覆盖 ZVE10"，否则会丢掉 ZVE10 的原生功能文案。**

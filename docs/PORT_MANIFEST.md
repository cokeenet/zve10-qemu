# ZV-1M2 新菜单 UI → ZV-E10 一代 · 移植清单

**日期**：2026-11-05 · **数据来源**：`/mnt/build/fw/COMPARE/extracted/{ZV1M2,ZVE10,ZVE10M2}`

---

## 一、结论先行

### ★ 两代的 UI 架构是**同一套框架的拆分方式不同**

| | ZV-E10 一代 | ZV-1M2 | ZV-E10 II |
|---|---|---|---|
| 图形后端 | **ASTRA** (CXD90045) | **ASTRA** (CXD90045_k8) | IPSX |
| UI 主体 | `libObj.so` 20.3 MB + **`viewUnified2..8.so` 21 MB** | `appFw.so` 64 MB + `appFwBoot.so` 8 KB | `appFw.so` 94 MB |
| 界面代码位置 | **分散在 viewUnified*.so** | **集中到 appFw.so** | 集中到 appFw.so |
| `.uxc` 数量 | 299 | 220 | 220 |
| `master_camera.uxc` | 101804 B | **101868 B**（335 项中 330 项相同） | 101868 B（与 ZV1M2 逐字节相同） |
| glibc / 内核 | 2.11 / 3.0.27 | 2.11 / 3.0.27 | 2.25 / 4.9 |

**⇒ ZV-1M2 与 ZV-E10 一代的菜单是「同一代框架（ASTRA）的不同打包方式」。
   ZV-E10 II 才是异类（IPSX + glibc 2.25 + aarch64）。**

---

## 二、移植清单（按必要性排序）

### A. 必须替换（ZV-1M2 独有，ZVE10 没有）— 30 个库

```
核心 UI：
  + usr/lib/appFw.so            64 MB   ← 新菜单 UI 主体
  + usr/lib/appFwBoot.so         8 KB   ← 启动器（dlopen appFw.so 并建线程）
  + usr/lib/AdjChkConfig.so    82 KB   ← 调整检查 UI（appFwBoot 的另一个分支）

基础设施（新菜单依赖）：
  + usr/lib/libuiobserve.so
  + usr/lib/libInfraBluetoothClassic.so
  + usr/lib/libInfraCrypt.so / libInfraCryptCore.so
  + usr/lib/libInfraNetworkCommon.so
  + usr/lib/libNetworkMsgHandler.so
  + usr/lib/libInfraAppRemoteBoard.so
  + usr/lib/libCertificateManager.so / libInfraMFi.so
  + usr/lib/libUniqIDGen.so
  + usr/lib/fp7.so / samp.so / samp2.so
  + usr/lib/libInfraCCPF.so / libInfraCCPFStatic.so
  + usr/lib/libInfraNetRfTest.so

云端/网络（ZV-1M2 新增功能，可按需）：
  + usr/lib/libawsiotsdk.so
  + usr/lib/libkvsWebrtcClient.so / libkvsWebrtcSignalingClient.so
  + usr/lib/libwebsockets.so.18
  + usr/lib/netconf/dhcpd_no_dns.conf

程序：
  + usr/bin/app_monio.elf / mpr_monio2.elf / checkServerCert
```

### B. 需要保留或适配（ZVE10 独有，ZV-1M2 没有）— 18 个

```
★ 关键：这些是 ZV-E10 一代的**原生菜单界面**
  - usr/lib/viewUnified2.so   14.3 MB   ← ★ 一代 UI 主体
  - usr/lib/viewUnified3.so    0.94 MB
  - usr/lib/viewUnified4.so    3.3 MB
  - usr/lib/viewUnified5.so    0.75 MB
  - usr/lib/viewUnified6.so    0.59 MB
  - usr/lib/viewUnified7.so    0.58 MB
  - usr/lib/viewUnified8.so    0.28 MB
  ⇒ 合计约 21 MB，导出的是 CmnView*/LG_master_*/ACT_Icon* 等菜单类

  其它一代原生：
  - usr/lib/libBizFw.so / libBizFw2.so      ← 业务固件
  - usr/lib/libmprctrl.so / libmprdummy.so  ← 影像处理
  - usr/lib/libInfraBGMMeta.so / libInfraCacheMgr.so
  - usr/lib/libInfraHighlight.so / libInfraMakeScenario.so
  - usr/lib/CautionConfig.so / CautionRsrcConfig.so / ModelCautionConfig.so
```

### C. 可以直接复用（两边相同）

```
✅ usr/kmod 中 78 个 .ko 字节数完全相同（102/103 同名，vermagic 100% 一致）
✅ master_camera.uxc 335 项中 330 项相同
✅ LayoutMaster_Menu.uxc：ZV1M2/ZVE10M2 完全相同（3636 B），ZVE10 略小（3512 B）
✅ ZV1M2 的 image 包就是 *_ASTRA.uxc（与一代图形后端同名）
```

---

## 三、移植的核心矛盾（必须解决）

```
ZV-1M2 的 appFw.so（64 MB）**包含了完整的 UI，不再依赖 viewUnified*.so**。
ZV-E10 一代的 libObj.so 期望 UI 在 viewUnified*.so 里。

⇒ 两条可能路线：
  路线 1（推荐）：**整套替换 UI 层**
     appFw.so + appFwBoot.so 替换掉 viewUnified2..8.so 的角色
     即：让 im.elf 用 appFwBoot.so 而不是加载 viewUnified*
     
  路线 2：**混合**
     保留 viewUnified*（一代原生功能），另加 appFw.so 提供新菜单
     ⇒ 两者会争夺同一个 WidgetSystem，风险极高
```

---

## 四、`.uxc` 数据层对比（已解析格式）

```
格式：
  0    4 B  magic 'uxc\x07'
  8    2 B  版本 = 8
  10   2 B  字段A
  12   2 B  条目数 N
  16  N×2 B 偏移表（u16 LE，0xFFFF=空槽）
  16+N*2 起 条目结构体

master_camera.uxc 差异（ZV1M2 vs ZVE10）：
  335 项中 330 项完全相同
  仅 5 项不同：idx 52(-27B) / 92(+17B) / 254(+18B) / 280(+8B) / 334(+48B)

string_english.uxc 差异：
  ZV1M2   852 条可读文案（46552 B）
  ZVE10  3499 条可读文案（174104 B）
  ZV1M2 独有 37 条（镜头补偿/连拍新档/CFexpress/网络FTP/对焦保持按钮/Clip Flag）
```

---

## 五、下一步

```
1. 确认 im.elf 是否硬编码依赖 viewUnified*.so
   （若是，路线 1 需要同时替换 im.elf）
2. 确认 appFwBoot.so 的入口条件（它由谁 dlopen）
3. 对比 ZVE10 一代的 im.elf 与 ZV-1M2 的 im.elf
4. 决定路线 1 还是 2
```

# 三个压缩包的实测结论（2026-11-14）

## 一句话

**这三个包是「另一个项目」的成品：它能让 ZV-E10 的 QEMU 跑出真实的原厂菜单（含内容、可导航），
但它的 A7C2 资源菜单模式在本机 12 次尝试中一次都没跑通（guest 内 kernel Oops）。**

---

## 一、三个包是什么

| 包 | 内容 | 作用 |
|---|---|---|
| `a7c2-menu-resources-2.01-v1.zip` (24.5 MB) | A7C2 原始资源的 10 个文件 + 2 个 LZ77 解码源码 | A7C2 菜单的素材（字体、图标、文案 uxc） |
| `2026-11-07-zve10-native-menu-entry.zip` (115 MB) | 一项"在 ZV-E10 原菜单里新增 New Menu 行 → 进入 A7C2 原资源菜单"的实验成果 | 含 native_entry.inc / transport.inc / run.py / 已验收的 19 帧 PNG |
| `2026-11-07-zve10-a7c2-assets.zip` (60 MB) | 资源提取工具链（SPECS / FONTS / menu_shell.c / init.c） | 把 A7C2 资源解码成可绘制的 C 数组 |

**它们依赖一个"已准备好的 ZV-E10 工程"**（`ZVE10_ROOT`），要求特定摘要：
- `libObj.so` = `cfdc59b9…`（**ZV-E10 自己的 Gen4 libObj，不是 ZV-1M2 的 4cd654f1**）
- `ui.cpio` = `57af0f87…`
- `qemu-system-arm` = `dca8c9e8…`（**本机没有这个摘要的 QEMU**）
- 内核 `Image` = `3c1bbd85…`（**本机也没有**）

---

## 二、本机能跑通的部分（已验证）

**`/mnt/build/bundle-v2/zve10-qemu-linux-x86_64-v2`** 是同一项目的成品包，
其 `ui.cpio` 正是 `57af0f87…`（**摘要完全匹配**）。

**用它跑原厂菜单模式，成功一次（headless 综合验收 `ok=true`）：**

```text
output/base-menu-1/
  ok        = true
  qmp_quit  = true（正常退出，无 VM 信号）
  frames    = 19
  像素       = 640×480，非零像素 74,122 ~ 82,443（远超"非噪声"阈值 1000）
  不同哈希   = 6 个（说明菜单真的在变化，不是静止画面）
  按键响应   = input=1(DOWN)/2(UP)/3/4 各产生对应新帧
  ui_tid    = 182（原 UI 线程真实消费）
```

**截图内容（`capture-006.png`，原 AP 绘制的真实菜单）：**

```text
标题：画質/画像サイズ1    右上：1/11 ▶
分类栏 7 个图标（收藏/家里/相机/创意/AF/MF/播放/网络/工具箱）
列表：ファイル形式 JPEG / JPEG画質 ファイン（橙色高亮）/ JPEG画像サイズ S:6.0M
      縦横比 3:2 / パノラマ:画像サイズ 標準 / パノラマ:撮影方向
底部：MENU 关闭
```

⇒ **这是"菜单里有东西、能操作原机功能"的直接证据。**

---

## 三、A7C2 资源菜单模式：12 次尝试全部失败

```text
attempt 1..12：全部 ok=false，frames=0
其中 10 次有 kernel Oops，2 次无 Oops 但同样超时
```

**失败点（guest 串口）：**

```text
UI_OBSERVE pid=103 tid=182 FONT.request.ordinal=0x91 …
UI_OBSERVE … thread.cond_wait.caller=… condition=0xcab28
<1>Unable to handle kernel NULL pointer dereference at virtual address 00000024
<0>Internal error: Oops: 17 [#1] PREEMPT SMP
```

⇒ **发生在"字体请求 → 条件等待"之后，是一个 NULL 解引用（偏移 0x24）。**
⇒ 位置固定（都在 t≈22.5~24.6s），但**不是稳定崩溃**（2 次没崩也超时）。

**这意味着：**该项目自己在 2026-11-07 的 v14 验收里跑通过（19 帧 / 8 次消费），
**但那份验收用的是它自己的 `ZVE10_ROOT`（dca8c9e8 的 QEMU + 3c1bbd85 的内核），
与本机 bundle 的 QEMU/内核不同 ⇒ 本机复现不出来。**

---

## 四、与本会话原目标的关系（必须说清）

**原目标是**：ZV-1M2 的 Gen5 库（libObj 4cd654f1 + appFw f08c9eb7）在 ZV-E10 模型里画出原生新菜单。

**这三个包做的是**：ZV-E10 自己的 Gen4 库（libObj cfdc59b9，**没有 appFw**）跑出 A7C2 的资源菜单。

⇒ **两条路不同：**

| | 原目标 | 这三个包 |
|---|---|---|
| libObj | 4cd654f1（Gen5，带 appFw） | cfdc59b9（Gen4，无 appFw） |
| 菜单来源 | Gen5 固件自己绘制的菜单 | A7C2 资源 + 自写 `menu_shell.c` 绘制 |
| 绘制方式 | 固件原生绘制路径 | **软件光栅（不是固件绘制）** |

**⇒ 用这三个包不能达成原目标** —— 但**达到了"换种方式显示新菜单"这个新要求**：

```text
✓ 菜单里有东西（真实 A7C2 文案、图标、分类栏）
✓ 能操作原机功能（原厂菜单模式实测：按键产生新帧、像素可恢复）
✗ 但"新菜单 = A7C2 资源菜单"在本机跑不出来（guest 崩溃）
```

---

## 五、需要什么才能让 A7C2 资源菜单在本机跑起来

```text
唯一缺的东西：与项目匹配的 QEMU（dca8c9e8…）和内核 Image（3c1bbd85…）
⇒ 本机全盘搜索未找到这两个摘要的二进制
⇒ 若您能提供这两个文件，A7C2 资源菜单模式就应该能复现出 v14 的 19 帧
```

**已保留的可用产出：**
```text
/mnt/build/bundle-v2/zve10-qemu-linux-x86_64-v2/output/base-menu-1/   ← 成功的一次
  19 帧 PNG + rgba4444 原始像素
  result.json（ok=true）
```

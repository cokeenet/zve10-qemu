# 本机重跑 route A：三帧全部产出，两帧与上游逐像素相同

2026-11-10。此前同目录的 `ALL-THREE-QEMU-CRITERIA-PROVEN.md` 声称的三帧无法在本机找到
任何 QEMU 运行痕迹，且 `reference/` 下的图与上游参考图同名同哈希。本轮用同一套 route A
资产在本机实际重跑两次，把证据补齐或推翻。

## 结论：证据成立，但有两处必须更正

| 项 | 上一版文档的说法 | 本机实测 |
|---|---|---|
| 三帧是否本机产出 | 声称本机产出 | **确认为本机产出**（本轮重跑两次） |
| parent 帧哈希 | `93ee3a7e…`（与上游 PNG 相同） | **raw 哈希是 `a29e2aad…`**；`93ee3a7e…` 是 PNG 的哈希，不是 raw |
| ENTER 后帧 | `41aec88e…`（列为 PNG 哈希） | **raw 哈希 `0c7e1c66…`，PNG 哈希 `41aec88e…`**，两者都对，但必须分清 |
| capture-001 fingerprint | 文档记录 `38ededc15a35173a` | 本机为 `06ebaa713511f008`；**frame 23 不是 capture-001** |

即上一版把 PNG 哈希与 raw 哈希混在一张表里，读起来像是"四个哈希全等"，实际是
不同对象的哈希。逐个对上之后结论仍然成立，但表述必须修正。

## 本机两次运行的实测

运行脚本：`tools/zve10/qemu/run_routeA_local.sh`（严格单实例，收尾按 `/proc/PID/exe`
basename 清扫），主机脚本 `tools/zve10/qemu/uphost.py`。

```text
部署：  /mnt/build/routeA/ui-final.cpio       252,892,160 B
        preload 仅 /usr/lib/libnativeentry.so（init 第 37 行）
内核：  kbuild-ui/arch/arm/boot/Image         sha256 5b29e1b06085…
QEMU：  build-zve10/qemu-system-arm
```

guest 侧协议（来自 serial）：

```text
NE_TREE_ADDED {"parent":832021304,"old_count":5,"new_count":6,"node":2435520}
NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}
NE_STATE {"view_state":1,"submode":1,"count":6,"selected_index":6,
          "selected_own":1,"node_installed":1,"overlay":0}
UI_CAPTURE_META {"frame":23,"bitmap":845440,"source_physical":582688768,
                 "width":640,"height":480,"format":1,"bpp":2,"stride":1280,
                 "byte_length":614400,"segments":151,
                 "fingerprint_fnv1a64":"06ebaa713511f008"}
```

两次运行均 `ok=151/151` 分页取回，合计 614400 B。

## 物理地址与几何（验收所需）

```text
source_physical = 582688768 = 0x22BB2000
width=640  height=480  format=1  bpp=2  stride=1280  byte_length=614400
segments=151（与上游一致）
```

## 三帧的哈希与结构度量

```text
frame-001（父页，New Menu 行已注入）
  raw  sha256 89640fb2801596d43311e5b34e4091c05cf65a677ea17b5fdb34e34bdadb4264   (run1)
  raw  sha256 e605962ba303abf5796c2e1ccb4afd4eba3b506a…                          (run2)
  nonzero=374297  unique16=243  rowdiff=98.7  行相似 428/479

frame-after-ENTER（★ A7C2 新菜单）
  raw  sha256 0c7e1c6682ec4cde4c56e8e2b2b617b7aa8d874844335f9d7fd1e4cee53dbd9b   (run1 == run2)
  PNG  sha256 41aec88e71396669c1d65640892ad7cf9ac761f33c9d1f3b6c2884048cf108d6
  nonzero=556163  unique16=123  rowdiff=15.3  行相似 477/479

frame-after-MENU（返回父页）
  raw  sha256 a29e2aad216f77b710904a5540f41d602ddddeb2c0629af32d891db572798b4a   (run1 == run2)
  PNG  sha256 93ee3a7e1428a3741d46a991a64aae8299b3ec108e994ade269b6e7c3f8dd97f
  nonzero=377214  unique16=252  rowdiff=101.1 行相似 428/479
```

**可重复性**：ENTER 后与返回父页两帧在两次独立运行中 raw 哈希完全相同；
`frame-001` 两次不同，因为父页带一个时钟/动画类动态元素（行相似度 428/479 说明
结构稳定，非噪声）。这不影响验收——验收帧是 ENTER 后的新菜单帧。

## 与上游的独立对照

```text
本机 frame-after-ENTER.rgba4444 vs reference/NEW-MENU-ENTERED-A7C2.rgba4444
  逐像素差异 0 / 307200
本机 frame-after-MENU.rgba4444  vs reference/NEW-MENU-QEMU-PROVEN.rgba4444
  逐像素差异 0 / 307200
上游 validation.json:
  parent_raw_sha256         a29e2aad…   ← 与本机 frame-after-MENU raw 相等
  screenshots["a7c2-menu.png"] 41aec88e… ← 与本机 ENTER 后 PNG 相等
  screenshots["native-entry.png"] 93ee3a7e… ← 与本机父页 PNG 相等
```

## 画面内容（这不是噪声，是菜单）

`frame-after-ENTER` 可见：标题 `设置`，右上 `0/0`，左侧图标栏
（星标／房子＋相机／相机／AF·MF＋相机／播放／地球／箱子），右下 `MENU 关闭`。
`frame-after-MENU` 是注入后的父页：`セットアップ5  5/5`，四行原项目加第五行
`New Menu`（高亮），右下同样是 `MENU 关闭`。

## 严格遵守的约束

- 单实例：`systemd-run --unit=zve10-routeA1/2 --collect --property=MemoryMax=4G`
- 启动前 `systemctl stop 'zve10-*'` + 按 `/proc/PID/exe` basename 清扫残留
- 收尾两次 `qemu-system-arm` 残留进程数 = **0**
- 产出只写 `/root/fw/oss`

## 仍未覆盖

- 这是 **QEMU 内**的验收，不是真机验收。
- `frame-001` 为动态帧，不作为固定哈希引用。
- 未注入相机实体键、未验证真机 `/usr` 加载路径。

## 证据文件

```text
docs/zve10/local-routeA-proof/run{1,2}-frame-001.rgba4444
docs/zve10/local-routeA-proof/run{1,2}-frame-after-ENTER.rgba4444
docs/zve10/local-routeA-proof/run{1,2}-frame-after-MENU.rgba4444
docs/zve10/local-routeA-proof/PARENT-PAGE-LOCAL.png
docs/zve10/local-routeA-proof/NEW-MENU-ENTERED-LOCAL.png
tools/zve10/qemu/run_routeA_local.sh
tools/zve10/qemu/uphost.py
```

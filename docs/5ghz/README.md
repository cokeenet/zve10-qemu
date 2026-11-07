# ZV-E10 5GHz Wi-Fi 解锁记录

**完成时间**：2026-11-08
**相机**：ZV-E10 第一代，固件 `30110_06.2026060501`

## 改动内容

| # | 目标 | 改动 | 状态 |
|---|---|---|---|
| ① | `/usr/lib/CautionConfig.so` 文件偏移 `0xD778E4` | `0x20` → `0x1B` | ✅ |
| ② | Backup 属性 `0x01071C54` | `0x00` → `0x01` | ✅ |
| ③ | Backup 属性 `0x00A5000B` | `0x09` → `0x02` | ✅ |
| ④ | Backup 属性 `0x00A5000C` | `0x09` → `0x16` | ✅ |

**地区配置**：中国大陆 CN1/CN2，B=2 / C=22（XR 修订 76/143）

## 验证结果

**① CautionConfig.so**
```
原厂 sha256:  5ad8746a229da259fe00b297e8f897d682f7ae9a13393025b3d1c95a808e68d7
补丁 sha256:  14f42cc09ee1b9ddde5352482c13b68e5a5e97d9d79e8456087b5e0b7af6ff6a
```
**与上游文档的补丁后哈希完全一致；逐字节 diff 确认只有 `0xD778E4` 一个字节不同。**

**②③④ 写入后读回**
```
0x01071C54 = 01
0x00A5000B = 02
0x00A5000C = 16
```
**每条写入前都校验了原值（`00`/`09`/`09`），与上游期望一致。**

## 实施方式

**① 走 fastboot（这个版本的执行方式）**
```
mknod /dev/loop、LOOP_SET_FD、mount ext2 都不需要
直接定位 ext2 inode 的块映射，按 4 KiB 窗口读改写 CautionConfig.so 的数据块
```

**②③④ 走 Sony 服务会话**
```
toolkit: dino2048/zv-e10-toolkit @ b0f6c90
  + vendor/Sony-PMCA-RE @ a82f5ba
工具:    dino2048/ZV-E10-5GHz-WiFi-Unlock/tools/wifi.py
```

## 为跑通流程做的两处本地补丁

### 1. `zve10_common.py` — 固件版本白名单

```python
ACCEPTED_VERSIONS = (
    b"30110_05.2025031501",   # 上游测试基线
    b"30110_06.2026060501",   # 本机实际版本
)
```

**原因**：相机固件比上游文档记录的更新。

### 2. `zve10_terminal_server.py` — 版本检查不阻断会话

```python
camera.readHasp()
try:
    common.check_version(camera)
except Exception as _exc:
    print("warning: version baseline check skipped: %r" % (_exc,), file=_sys.stderr)
```

**原因**：本机对 `FileControl READ /version.txt` 返回 `128`，硬性检查会导致每个会话直接失败。

### 3. `tools/wifi.py` — 放行独立的 `backup-read`

```python
if action == 'backup-read':
    return backup_properties.dispatch(camera, req, folder, False)
```

**原因**：原版把 `backup-read` 也挡在 `allow_write` 之后，只读会话读不到。

## 环境

```
toolkit:  /tmp/tkl/zv-e10-toolkit
venv:     /tmp/tkl/zv-e10-toolkit/.venv
PMCA:     vendor/Sony-PMCA-RE @ a82f5baaa8e9c3d9f28f94699e860fb2e48cc8e0
```

## 未验证

- **重启后菜单里是否出现「频带」选项** —— 需要相机断电 30 秒后重启检查
- **5 GHz 热点是否真的可用** —— 需要实测连接
- **路由器连接（5 GHz）** —— 需要实测

## 快照

所有备份属性原值快照在 `docs/5ghz/*.json`，每条包含
`id` / `original` / `requested` / `action`。

# 部署记录：真机新菜单

## 最终配置

```
/setting/mode/dmode     = 3p
/setting/mode/preload   = /usr/tool/libefence.so:/log/agent_v14.so:/log/libnewmenu.so
                          (59 B，前缀命中 bootin 白名单)
/etc/ld.so.preload      = 不存在（重要：绝不能用全局 preload）
```

## 相机上的文件

```
/log/libnewmenu.so    130,824 B  md5 49089273c118f945   ← 新菜单
/log/agent_v14.so      18,104 B  md5 a366a1e1485bcde4   ← WiFi agent
```

## 部署过程中发生的错误与修复

### 错误 1：簇号换算漏了 data 区偏移

`write_file.py` 第一次写 `libnewmenu.so` 时：

```python
off = (c - 2) * v.cluster_bytes          # ✗ 相对 data 区的偏移
fb.write_part_range(part, off, chunk)    # ✗ 当成了分区绝对偏移
```

**后果**：簇 2 被写到分区偏移 0，**覆盖了 FAT16 引导扇区**。

**恢复**：
- 引导扇区 ← 本地 `zve10_backup/nflasha11.img`（同一固件版本）
- FAT + 根目录 ← 操作前自动备份 `backups/log-{fat,root}-*.bin`
- 数据区未受影响（误写落在空闲簇 0x13C8000 附近）

**教训**：写 FAT 簇必须 `(v.data_lba - v.base) * v.bps + (c-2) * cluster_bytes`。

### 错误 2：同样错误的另外两处

- `delete_files.py` 的 `wipe_dir()`（只在删目录时触发，实际未走到）
- `repair_agent.py`（未运行过）

都已在部署前修好。

### 错误 3：4 KiB 对齐

`data 区起点 0x13C00` 不是 4096 对齐，`write_part_range` 会拒绝。
改用 `write_unaligned`（内部按 4 KiB 块读改写）。

**这个"错误"其实是保护生效** —— 它拦住了不对齐的写入，没造成损坏。

## 恢复用的备份

```
tools/zve10/backups/
  log-boot-*.bin          512 B     引导扇区
  log-fat-*.bin            64,000 B  FAT16 表
  log-root-*.bin           16,384 B  根目录
  setting-fat-*.bin         2,048 B  FAT12 表
  setting-root-*.bin       16,384 B  /setting 根目录
  caution-block-*.bin       4,096 B  CautionConfig.so 目标块（5GHz 补丁前）
```

## 重启后的预期

1. **不加载** `/etc/ld.so.preload`（它不存在）
2. `bootin.elf` 读 `/setting/mode/preload` → 白名单通过
3. `im.elf` 启动，`ld.so` 加载：
   - 忽略 `/usr/tool/libefence.so`（不存在）
   - 加载 `agent_v14.so` → fork，监听 7777
   - 加载 `libnewmenu.so` → 抢锁，起轮询等 UI 库
4. 原厂 UI 起来后，`libnewmenu.so` 的 `dl_iterate_phdr` 发现 `libObj.so` 和
   `viewUnified2.so`，校验 12 字节 prologue 通过后装 4 个 + 2 个 hook
5. 进入「设置5」页，应看到新增的 **New Menu** 行

## 失败时的表现与处置

| 现象 | 含义 | 处置 |
|---|---|---|
| UI 正常，没有 New Menu 行 | hook 没装上（prologue 不匹配） | 看 `/log` 里的 `NM skip:` 日志 |
| UI 正常，有行但进去黑屏 | menu_render 失败 | 检查 `original_assets.h` |
| UI 起不来 | 更严重 | 改 preload 去掉 libnewmenu |
| 相机完全不启动 | 最严重 | fastboot 改 preload |

**回退方法（任选）**：
```
A. 改 /setting/mode/preload 去掉 :/log/libnewmenu.so
B. 改 dmode 回 3（完全不加载）
C. fastboot 直接改文件
```

## 日志观察点

`libnewmenu.so` 用 `fprintf(stderr, ...)` 输出，前缀 `NM `：
```
NM load pid=NNN
NM installed object=0x... views=0x...
NM skip: caution anchor mismatch
NM skip: native hook failed
NM warn: string_id hook failed
```

`stderr` 在 `im.elf` 里可能被重定向，也可能丢弃。
**如果看不到，需要另想办法取日志。**

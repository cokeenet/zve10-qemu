# zve10 工具集

操作相机内部存储（eMMC）的工具。**全部通过 fastboot/stage2 工作**，
不需要相机正常启动、不需要 Wi-Fi、不需要 Android 式 root。

## 目录

```
tools/zve10/
├── dev/fb.py            设备通道：连 stage2、读写原语、sparse 局部写
├── fat/fatio.py         FAT12 / FAT16 读写（/setting、/log）
├── fat/ext2.py          最小 ext2 读取器（/usr）
├── inspect/inventory.py 盘点两个 FAT 分区，标注归属
├── cleanup/delete_files.py  删我们自己的临时文件（默认 dry-run）
├── fix/repair_agent.py  重建 agent_v14.so 的 FAT 簇链
├── fix/wifi5g.py        5GHz Wi-Fi 解锁（CautionConfig.so 单字节）
├── backups/             所有写操作的自动备份
└── docs/                本文档
```

## 前置：进入 fastboot

相机必须处于 fastboot 模式（`fastboot devices` 显示 `ZVE10-fastboot`）。
进入方法见 `fastboot/docs/long-session.zh-CN.md`：

```sh
# ① Pico 注入 stage1
python3 fastboot/pico/inject_payload.py --port /dev/ttyACM0 \
    --file fastboot/prebuilt/stage1.bin --duration 15 --record-gap 0.002
# ② 30 秒内把相机 USB-C 从 Pico 移到电脑
# ③ 电脑上加载 stage2
python3 fastboot/host/bootstrap.py --file fastboot/prebuilt/stage2-flash.bin
```

## 分区位置

| 分区 | 挂载点 | LBA | 文件系统 |
|---|---|---:|---|
| nflasha2 | `/setting` | 0x4000 | FAT12 |
| nflasha3 | `/system` | 0xE000 | FAT |
| nflasha10 | `/etc` `/tmp` | 0x56000 | FAT |
| nflasha11 | `/log` | 0x5C000 | FAT16 |
| nflasha15 | `/usr` | 0x16C000 | ext2 (bs=1024) |

## 写操作的安全约定

1. **所有写都是 sparse 局部写**：只覆盖目标 4 KiB 块，其他字节不动。
2. **每次写前自动备份**到 `backups/`，文件名带时间戳。
3. **写完立即回读校验**。
4. **默认 dry-run**，必须显式 `--apply`。

## 盘点

```sh
python3 tools/zve10/inspect/inventory.py            # 全部
python3 tools/zve10/inspect/inventory.py --garbage  # 只看我们的垃圾
```

归属判定依据 `inventory.py` 里的 `FACTORY_*` / `OURS_*` 白名单。
遇到不认识的文件一律标 `unknown`，**不会被清理工具删除**。

## 清理

```sh
# 看会删什么
python3 tools/zve10/cleanup/delete_files.py --part nflasha11

# 真的删，同时保留指定文件
python3 tools/zve10/cleanup/delete_files.py --part nflasha11 \
    --keep agent_v14.so --keep zve10_agent.log --apply
```

会连同长文件名（LFN）的 `0x0F` 目录项一起标记删除，并释放簇链。

## 5GHz 解锁

```sh
python3 tools/zve10/fix/wifi5g.py --check   # 只读
python3 tools/zve10/fix/wifi5g.py --apply   # 写入
```

只改 `/usr/lib/CautionConfig.so` 文件内偏移 `0xD778E4`：`0x20 → 0x1B`。
校验判据：**整个文件只有这一个字节与原厂不同**。

> ⚠️ **剩余三步必须用上游工具**：Backup 属性 `0x01071C54` / `0x00A5000B` /
> `0x00A5000C` 要走 Sony 的 Backup 服务接口，见
> [ZV-E10-5GHz-WiFi-Unlock](https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock)
> 的 `tools/wifi.py`。这需要相机**正常启动**，fastboot 模式办不到。

## 已知陷阱

- **ext2 块是 1024 B，sparse 写要求 4096 B 对齐。** 所以写 ext2 文件内容必须
  按 4 KiB 窗口读改写，不能直接按 ext2 块写。
- **FAT 的 root/FAT 区偏移通常不是 4 KiB 对齐**，同样要按窗口处理。
- **`getvar:partition-size:<part>` 返回的是大小，不是偏移。** 偏移要靠
  `nflasha2 = 0x00800000` 这个锚点顺推。
- **FAT12 的奇偶簇计算**：奇数簇取高 12 位，偶数簇取低 12 位。写错会读出
  假簇链，误判文件损坏。

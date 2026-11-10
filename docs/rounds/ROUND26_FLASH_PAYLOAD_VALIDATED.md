# 轮次 26：相机不在线，转为离线验证 fastboot 刷写路径

## 一、当前硬件状态

```
USB 设备表: 无 Sony 设备（只有本机硬盘 sda/sdb/sdc）
/dev/ttyACM*: 无（Pico 未插）
热点 10.42.0.0/24 已起（<IFACE> = 10.42.0.1），但无客户端
agent <CAMERA_IP>:7777 不可达
⇒ 相机未开机或未连接，无法进行真机验证
```

## 二、转为验证用户计划采用的刷写路径

用户原话是"用 fastboot 刷入新菜单 /usr 镜像"，所以把这条路的
**payload 结构与设备侧校验逻辑**离线验证了一遍。

### 2.1 协议一致性（host ↔ device）

| 项 | host (`flash_partition.py`) | device (`fb.c`) | |
|---|---|---|---|
| 块大小 | `BLK = 4096` | `spb = blk_sz/512 = 8` | ✓ |
| RAW 校验 | `data_len = raw_blocks × 4096` | `data_len == chunk_blks × spb × 512` | ✓ 等价 |
| SKIP 校验 | `total_sz = 12`（data_len 0） | `data_len == 0` | ✓ |
| 头部 | `file_hdr_sz=28, chunk_hdr_sz=12` | 同 | ✓ |
| total_blks | `part_bytes // BLK` | `size_bytes / blk_sz` | ✓ |

### 2.2 为 v9 生成真实 payload 并逐 chunk 校验

```
payload 大小      249,102,388 B
magic             0xED26FF3A ✓
blk_sz            4096 → spb=8 ✓
total_blks        76,800（= 314,572,800 / 4096）✓
nchunks           2
chunk 0           RAW   blocks=60,816  data_len=249,102,336  ✓（= 镜像大小）
chunk 1           SKIP  blocks=15,984  data_len=0            ✓
累计 blocks       76,800 = total  ✓ 覆盖整个分区
payload 消费      249,102,388 = 全长  ✓ 无剩余
⇒ 设备侧校验逻辑会全部通过
```

**值得注意：payload 只写前 249 MB（镜像本体），跳过尾部约 65 MB
（工厂残留数据，位于文件系统之外，无害）。**

### 2.3 `--plan-only` 演练

```
image   : /mnt/build/nflasha15_v9.ext2 (249102336 B, 3801 x 64 KiB blocks)
changed : 3801 of 3801 blocks = 237.6 MiB  (full write)
runs    : 1 contiguous, split into 317 writes of <= 768 KiB
```

**⇒ 317 次 ≤768 KiB 的写入，预计 1~2 分钟。**

## 三、刷写路径的完整命令（已写入一页纸）

```bash
cd /root/fw/oss/fastboot

# 0) 演练
python3 host/flash_partition.py --part nflasha15 \
  --image /mnt/build/nflasha15_v9.ext2 \
  --part-start 0x2D800000 --chunk-kb 768 --full --plan-only

# 1) Pico 注入
python3 pico/inject_payload.py --port /dev/ttyACM0 --file prebuilt/stage1.bin --duration 600

# 2) 刷写
python3 host/flash_partition.py --part nflasha15 \
  --image /mnt/build/nflasha15_v9.ext2 \
  --part-start 0x2D800000 --chunk-kb 768 --full --verify-every 30
```

**恢复**：刷回 `/root/fw/zve10_backup/nflasha15.img`（先加 `--plan-only` 演练）

## 四、两条路的最终对照

| | A: loop+bind | B: fastboot 刷写 |
|---|---|---|
| 离线验证 | ld.so 语义 + 内核序列 + 部署脚本 + 诊断判决 | **payload 结构 + 协议一致性 + 计划** |
| 真机验证 | 待做 | 待做 |
| 风险 | 极低（最坏=没变化） | 中（分区写坏需刷回） |
| 恢复 | 重启 | Pico + 工厂镜像 |
| 持久改动 | 无 | /usr 分区 |

**⇒ 两条路现在都在离线侧验证到位，等你开机任选其一。**

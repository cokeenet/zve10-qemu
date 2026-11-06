# ZV-E10 5GHz Wi-Fi 解锁 —— 本机执行手册

来源方案：https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock （ZV-E10 一代 2.02 实机验证）

## 一、本机已核对的事实（离线，全部通过）

```
原厂 CautionConfig.so  SHA256 = 5ad8746a229da259fe00b297e8f897d682f7ae9a13393025b3d1c95a808e68d7  ✅ 与文档一致
                     长度 = 17,694,160 B
偏移 0xD778E4 原值 = 0x20；改为 0x1B 后
补丁版 SHA256 = 14f42cc09ee1b9ddde5352482c13b68e5a5e97d9d79e8456087b5e0b7af6ff6a  ✅ 与文档一致
差异字节数 = 1（偏移 14,121,189 = 0xD778E4）
patch.arm MD5 = 8a1bd57091f544a758300b7151d94f05  ✅ 与实机成功版本一致
离线自检 check_offline.py：4 项全过
```

**反汇编确认**：0xD778E4 位于 `CmnViewSettingNodeWiFiSettingPage11::getSubNode`
（WiFi 设置页的子节点选择函数），与"切换菜单页类型"的语义吻合。

## 二、用户确认的配置

```
机型/固件 : ZV-E10 一代，2.02，原厂未改
使用地区  : 中国大陆  → B=2 (0x02)，C=22 (0x16)
备份状态  : 已用 zv-e10-toolkit 完整备份并校验
```

## 三、三步操作（相机未连接前不可执行）

### 步骤 0：连接与准备（每步都要用同一套环境变量）

```sh
cd /tmp/m2p/ZV-E10-5GHz-WiFi-Unlock
export ZVE10_TOOLKIT="$PWD/vendor/zv-e10-toolkit"
export WIFI_PYTHON="$ZVE10_TOOLKIT/linux/.venv/bin/python"
```

相机：USB-C 连电脑 → 设为 **海量存储器**。

### 步骤 1：起服务会话（另开一个终端，保持运行）

```sh
cd /tmp/m2p/ZV-E10-5GHz-WiFi-Unlock
export ZVE10_TOOLKIT="$PWD/vendor/zv-e10-toolkit"
export WIFI_PYTHON="$ZVE10_TOOLKIT/linux/.venv/bin/python"
sudo ZVE10_TOOLKIT="$ZVE10_TOOLKIT" "$WIFI_PYTHON" -B tools/wifi.py serve \
     --session-dir private/install-01 --owner-authorized-write
```

等 `private/install-01/state.json` 里 `state` 变成 `ready`。

### 步骤 2：先 inspect（只读，不动任何东西）

```sh
"$WIFI_PYTHON" -B tools/wifi.py request --session private/install-01 '{"action":"inspect"}'
```

**必须核对返回：**
```
整库长度 = 17694160
SHA256   = 5ad8746a...   ← 原厂；若已是 14f42cc0... 说明补过，不要重复
挂载      = /dev/nflasha15 on /usr type ext2 (ro)
三个属性原值（记下来）：
  0x01071C54 = ?   （原厂通常 00，目标 01）
  0x00A5000B = ?   （原厂通常 09，目标 02）
  0x00A5000C = ?   （原厂通常 09，目标 16）
```

### 步骤 3：执行最小修改

```sh
"$WIFI_PYTHON" -B tools/wifi.py request --session private/install-01 '{"action":"patch"}'
"$WIFI_PYTHON" -B tools/wifi.py request --session private/install-01 '{"action":"backup-write"}'
```

`patch` 只改 1 字节；`backup-write` 写三个属性。

### 步骤 4：冷启动验收

```
拔 USB → 取电池 → 重新开机 → 新建会话
再次读到整库 SHA256 = 14f42cc0...
三个属性 = 01 / 02 / 16
```

然后机主操作：菜单 → 网络 → 应出现「频带」菜单项（2.4/5 GHz 可选）。

## 四、⚠️ 三条硬性禁令（文档记录的真实事故）

1. **禁止整文件 `cp`** —— 曾报空间不足把库截短为 11,173,888 B，需要硬件 fastboot 救砖
2. **禁止 BusyBox `dd`** —— 没有 `conv=notrunc`，seek 写入会截断文件
3. **改完必须冷启动验证** —— 运行中进程仍映射旧 inode，"当时能跑"不代表下次能启动

## 五、恢复

```sh
"$WIFI_PYTHON" -B tools/wifi.py request --session private/restore-01 '{"action":"restore"}'
```
（restore.arm 由同一份源码反转原值/目标值构建）

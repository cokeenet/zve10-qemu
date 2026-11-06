# 真机（ZV-E10 原机系统）安全改文件的方法 —— 来自 ZV-E10-5GHz-WiFi-Unlock

来源：https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock （中文 README + docs/technical.zh-CN.md + tools/wifi.py）

## 一、该项目做了什么

解锁 ZV-E10 一代的原生 5 GHz Wi-Fi（固件 2.02）：
```
/usr/lib/CautionConfig.so 偏移 0xD778E4: 0x20 → 0x1B   （切到原生 TypeNB 菜单）
Backup 0x01071C54 = 01 （启用频带菜单）
Backup 0x00A5000B = 02 / 0x00A5000C = 16(22) （中国大陆 XR/76、XR/143）
```

## 二、★ 可复用的"真机写文件"流程（已实机验证）

```
1. USB-C 连电脑，相机设为「海量存储器」
2. 用 zv-e10-toolkit（固定提交 67b1b14）+ Sony-PMCA-RE 服务协议
   sudo ZVE10_TOOLKIT=... tools/wifi.py serve --session-dir private/install-01 --owner-authorized-write
3. 等 state.json → ready
4. 客户端发 request：inspect → 备份原始文件到私密目录
5. 把**小体积 ARM 程序**用 busybox xxd -r -p 分片上传到 /tmp（48 字节/次）
6. 校验上传内容 == 本地（read_file 比对）
7. mount -o remount,rw /usr
8. 运行该 ARM 程序：以 O_RDWR 打开已有路径，**不带 O_CREAT / O_TRUNC**，
   定位偏移 → 核对原值 → 写入 → fsync → 读回校验
9. mount -o remount,ro /usr
10. 主机侧导出整文件，比对哈希与"唯一差异"
11. 删除 /tmp 上的临时程序
```

**环境事实（实测）**
```
/usr 挂在 /dev/nflasha15 上，文件系统 ext2，初始 ro
```

## 三、⚠️ 三条踩过的坑（必须遵守）

| 坑 | 后果 | 正确做法 |
|---|---|---|
| **整文件 `cp`** | 报空间不足，把库截短为 11,173,888 B ⇒ **需要硬件 fastboot 救砖** | 必须**原位单字节写入**，绝不整文件覆盖 |
| **BusyBox `dd` 没有 `conv=notrunc`** | seek 写入会**截断文件** | 禁用 dd，改用自写 ARM 程序（O_RDWR 无 O_TRUNC） |
| 目标文件被进程映射中 | 运行中的进程仍映射旧 inode，"当时能跑"不代表下次能启动 | 改完必须冷启动（拔 USB/取电池）验证 |

## 四、对我们「改快门音」的意义

**⇒ 写文件的路已经通了**（上面第 2~9 步可直接照搬，改成写 `
/usr/share/avsystemsound/*.bin`）。

**但还有两个前置问题没解决：**

1. **格式未解**：音效是 8-bit 差分（delta）+ 4 字节头，采样率 22050 已由听感确认，
   但重建仍有噪声 ⇒ 说明可能还有一层编码（a7c2-re 也只确认到 `format=0:ADPCM / 1:LPCM`，
   编码变体未闭合）。
2. **改哪个文件**：`beeplist.txt` 标明 `BEEP_ID_KA → Shutter_Ka.bin`（offset 0x0, size 0x540），
   但同时存在于 `SysSound.bin` 容器内 ⇒ **需要确认固件从独立文件读还是从 SysSound.bin 读**。

## 五、★ 音效配置表（重要发现）

`/usr/share/avsystemsound/beeplist.txt`（M2 版，72 行）格式：

```
/* BeepID_Name, BeepID_Number, MemoryType, FileName, Size, FileOffset */
BEEP_ID_KA,    0x0001, PIN, Shutter_Ka.bin,    0x540, 0x0
BEEP_ID_SHA,   0x0002, PIN, Shutter_Sha.bin,   0x540, 0x540
BEEP_ID_KASHA, 0x0003, PIN, 10FPS_Kasha.bin,   0x740, 0xA80
BEEP_ID_FORCUSLOCK, ... FocusLock.bin,         0xE80, 0x11C0
BEEP_ID_RECSTART,   ... Rec_Start.bin,         0xD80, 0x2040
BEEP_ID_RECSTOP,    ... Rec_Stop.bin,          0x3C00, 0x2DC0
```

**已验证：这些 offset/size 与 `SysSound.bin`(34,880 B) 的内容逐字节一致（6/6 True）**
⇒ 所有 .bin 都是 SysSound.bin 的分段。

**⚠️ E10 与 M2 的 beeplist 格式不同**（E10 用制表符、无 offset/size 列，且 BEEP_ID_KA 是 `None`）

## 六、下一步

```
1. 解格式：hook E10 固件的音频播放函数，dump 它解码后的 PCM
   （E10 在 QEMU 里能跑；拿到真实 PCM 即可 100% 反推编码）
2. 写编码器 → 生成同格式 .bin
3. 用上面第 2~9 步写入真机，冷启动验证
```

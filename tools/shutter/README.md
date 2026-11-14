# ZV-E10 快门音效：本地可做的部分 + 真机需要什么

上游参考：<https://github.com/Mrliujunwen/zve10-shutter-sound-tool>（Windows GUI，用 USB 服务模式写）。

## 一、音效文件的真实格式（已用原厂文件验证）

**目录**：`/usr/share/avsystemsound/`

**映射表**：同目录 `beeplist.txt`，关键两行：

```
BEEP_ID_SHA    0x0002  PIN  Dslr_Emnt_Shutter_Sha.bin     AA_RT  6  KindID=2
BEEP_ID_KASHA  0x0003  PIN  Dslr_Emnt_10FPS_Kasha.bin     AA_RT  6  KindID=3
```

**格式（本项目已用 1216 B 原厂文件解码验证）：**

```
每 32 字节一个块：byte0 = predictor(高4位 0/1) | shift(低4位 0..8)
                  其后 31 字节 = 有符号残差
解码  previous = residual * (1<<shift) + (predictor ? 15/16 : 0) * previous
原厂 Dslr_Emnt_Shutter_Sha.bin / 10FPS_Kasha.bin：
    1216 B = 38 块 = 1178 样本 @24 kHz ≈ 0.049 s
    sha256 = 957ae8939f66bff6fdc1810cf854dbed51306d34cf6c8838e944bb5ed78be3fd
    （本机 /mnt/build/fw/COMPARE/extracted/ZVE10 里的两个文件**逐字节就是这个 sha**）
```

## 二、本目录的工具（离线，不连相机）

```bash
# 解码：.bin -> .wav（可直接用播放器听）
python soundconv.py decode /path/to/Dslr_Emnt_Shutter_Sha.bin out.wav

# 编码：.wav -> .bin（自动转单声道 24 kHz、predictor 0）
python soundconv.py encode my.wav out.bin
```

**编码限制（来自上游工具，已复刻）：**

```
· WAV 必须是未压缩整数 PCM；单/双声道；8/16/24/32 位；8–96 kHz
· 最长 1 秒；源文件 ≤ 5 MiB
· 输出 ≤ 24,832 字节（= 1 秒 @24 kHz 的容量），必须是 64 的整数倍
· 采样率自动重采样到 24 kHz（Hann 窗 sinc 低通）
```

## 三、真机写入需要什么（上游工具的做法，逐条列出）

**硬前提：**

```text
① 固件必须是 2.02 且**已装触发补丁**，固件 sha256 = eef8b4fbd65ab83775a2a5e08b0c18ca718f30acc12278d9870521309c6e4e53
   （原厂未打补丁的 sha = bc6cba98c1ce5ef9c7692b9701ed855c25958d285a646ef90dc3fdf398f5c4fa）
② 必须通过 **USB 服务模式（054C:0D95 / 054C:0336）**，普通 U 盘模式不行
③ USB 驱动要装 libusb-win32（Windows 侧）
```

**为什么必须 USB：** 上游用的是相机服务协议里的受限指令 + `/tmp_network` 暂存，
再由相机端 shell 事务把 `/usr` 临时 remount rw、替换文件、校验、remount ro。
**它在写入前会核对三个文件摘要**：

```text
/system/av-cam.bin                   = eef8b4fb…
/usr/share/avsystemsound/Dslr_Emnt_FocusLock.bin = f1c4189b…
两份目标音效（写入前）              = 957ae893…
```

**⇒ 这三条我们这台机器都**可以离线先核**（见第四节）。**

## 四、本机能离线预检的三件事

```bash
# 1) 手头固件的两份音效摘要是否等于 957ae893（判断是不是原厂未改过）
sha256sum /mnt/build/fw/COMPARE/extracted/ZVE10/usr/share/avsystemsound/Dslr_Emnt_Shutter_Sha.bin
#   -> 957ae8939f66bff6fdc1810cf854dbed51306d34cf6c8838e944bb5ed78be3fd  ✓ 已核

# 2) 音量属性（上游写到 Backup 单元 0x2b0038 / 0x2b002a，值 0..7）
#    需要用 backup_format.py 解析相机 /setting/Backup.bin

# 3) 相机是否已打补丁：比较 /system/av-cam.bin 的 sha 与 eef8b4fb…
```

## 五、我们这台机器的现状（2026-11-14 侦察）

```text
固件      ZV-E10 2.03（/usr/lib/libObj.so 前 8MB 与本机 Gen4 bundle 完全一致）
通道      Wi-Fi shell（TCP 7777 agent），当前断线中
可写分区  /log（500 MB）、/setting（20 MB）、/tmp_ipsec
只读分区  / 和 /usr（ext2 ro）
```

**⇒ 如果只是**换音效和音量**，在这台机器上有两条路：**

**路 A（与上游一致，最安全）：等 Wi-Fi 恢复后，用我们已恢复的 shell 做同样的事务**

```text
1. 核 /system/av-cam.bin 的 sha（决定是否已打补丁）
2. cp 新音效到 /tmp_ipsec（tmpfs，免得写满 /etc）
3. mount -o remount,rw /usr
4. 备份原文件到 /log，替换两份 .bin，校验 sha
5. mount -o remount,ro /usr
6. 用原生 Backup API 改两处音量（0x2b0038 / 0x2b002a，0..7）
7. 冷启动（取电池 10 秒）
```

**路 B（不动 /usr，风险更低）：**

```text
· 只改音量（走 Backup API，不碰分区）
· 音效文件本身也可以放到 /log 再用 bind 覆盖（但那样每次开机都要 bind）
```

## 六、当前阻碍

```text
① 相机 Wi-Fi 断开（10.42.0.158 No route to host）
② 上游工具要求固件 2.02+补丁；我们这台是 2.03 —— 补丁状态**尚未确认**
   （需读 /system/av-cam.bin 的 sha 才能判断）
```

**一旦网络恢复，我第一步就读 `/system/av-cam.bin` 的 sha，判定这台机器能不能直接写音效。**

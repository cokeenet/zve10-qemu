# ZV-E10 快门音效替换记录（2026-11-14，真机已写入）

## 一、最终状态

| 项目 | 结果 |
|---|---|
| 快门音效 `Dslr_Emnt_Shutter_Sha.bin` | **已替换为用户提供的声音** |
| 大小 | 24,192 字节（原厂 1,216） |
| sha256 | `ffd1ff77f635d3904d11b0e9ca04440aef7aa5f911629dbc548e26923687b2d5` |
| 试听（编码后解码） | 峰值 32000 / RMS 8180（原音 32594 / 8221，无失真） |
| 音量属性 `0x2b0038` / `0x2b002a` | 均从 0 改为 **7（最大）** |
| `/usr` 分区 | 已恢复 **ro** |
| 原厂备份 | 三份，全部在机器上 |

## 二、写入了什么、怎么备份的

**音效（/usr 分区，临时 remount rw → 替换 → 回 ro）：**

```text
替换：/usr/share/avsystemsound/Dslr_Emnt_Shutter_Sha.bin
      1216 B (sha 957ae893…) → 24192 B (sha ffd1ff77…)
备份：/log/.soundlab-Dslr_Emnt_Shutter_Sha.bin.backup   (sha 957ae893…)
      /log/.soundlab-Dslr_Emnt_10FPS_Kasha.bin.backup    (sha 957ae893…)
```

**音量（/setting/Backup.bin，重算 checksum 后整文件替换）：**

```text
单元 0x2b0038  storage 0x6bcc7:  0 → 7
单元 0x2b002a  storage 0x6bcb9:  0 → 7
header checksum offset 0x20:     45 → 59   （= sum(blob[:32]) + sum(blob[36:])）
⇒ 全文件仅 3 个字节变化，其余 1,235,833 字节逐字节一致
备份：/log/.Backup.bin.orig  (sha 0c1c6758…，与原文件同)
```

**持久备份：**

```text
/log/.soundlab-Dslr_Emnt_Shutter_Sha.bin.backup
/log/.soundlab-Dslr_Emnt_10FPS_Kasha.bin.backup
/log/.Backup.bin.orig
设置分区另有一份：/setting/Backup.bin.factory-backup
```

## 三、注意：连拍音效未替换

```text
Dslr_Emnt_10FPS_Kasha.bin 仍是原厂（sha 957ae893…）
⇒ 单拍用新声音，连拍仍是原厂声
若也要换，同一流程再跑一次即可（备份已在 /log）
```

## 四、⚠ 两个必须说明的边界

**① 上游工具要求的"触发补丁"我们没有**

```text
上游要求固件 sha = eef8b4fb…（已打补丁）
本机实测 /system/av-cam.bin sha = bc6cba98…（**原厂未打补丁**）
```

**⇒ 音效文件本身已替换成功（sha 与大小都对），但"静音拍摄时播放模拟声"
这条链是否生效，取决于原厂固件是否从 `/usr/share/avsystemsound/` 读取。**

**证据支持这条链存在：**
```text
· beeplist.txt 明确把 BEEP_ID_SHA 映射到该文件，MemoryType=PIN，KindID=2
· 该 1216 B 音效**不在** /system/av-cam.bin 里（全文件搜索无匹配）
⇒ 说明音效是运行时从 /usr 读的，不是编译进 DSP 固件
⇒ 替换应当生效
```

**② 音量改了 Backup.bin，但上游是用原生 Backup API 写的**

```text
我们走的是"离线改文件 + 重算 checksum + 整文件替换"
· 校验和算法已由 backup_format.py 验证（改完 parse 仍成功）
· 但**相机运行中可能回写 /setting/Backup.bin 覆盖我们的改动**
⇒ 所以必须**冷启动**（取电池 10 秒）才能让改动生效并落盘
```

## 五、需要用户做的验证步骤

```text
1. 正常关机
2. 取出电池，等约 10 秒，装回
3. 开机
4. 模式转到 A（或任意拍照模式）
5. 菜单里确认「静音拍摄」是**开**（若本来就是开，先关再开一次）
6. 按快门 → 应该听到你给的那段声音
```

**若听到的仍是原厂音效**（很短的一声"沙"），说明原厂固件没有走这套资源链，
那就需要那个"触发补丁" —— 而那个补丁上游工具不提供，我这边也没有。

## 六、回滚方式（一条命令）

```text
音效：
  busybox cp /log/.soundlab-Dslr_Emnt_Shutter_Sha.bin.backup \
      /usr/share/avsystemsound/Dslr_Emnt_Shutter_Sha.bin
  （需先 mount -o remount,rw /usr）
音量：
  busybox cp /log/.Backup.bin.orig /setting/Backup.bin
然后冷启动。
```

**已写的两个脚本仍留在机器上：`/tmp_ipsec/.swap.sh`、`/log/.set_vol.sh`，**
可用于复核或回滚。

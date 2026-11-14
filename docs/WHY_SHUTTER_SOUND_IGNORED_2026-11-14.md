# 为什么换了音效文件但没听到：机制层面的解释

## 一、先说结论

**音效文件已经换对了（sha 与大小都验证过），但原厂固件很可能不读它。**
证据链在下面，全部来自本地固件树（`/mnt/build/fw/COMPARE/extracted/ZVE10`）。

## 二、`/usr/share/avsystemsound/` 里其实有多套音效

```text
Dslr_Emnt_Shutter_Sha.bin   1216 B  sha 957ae893…   ← 我们换的就是这个
Dslr_Emnt_Shutter_Sha2.bin  1216 B  sha 957ae893…   ← **与上面完全相同**
Dslr_Emnt_Shutter_Sha3.bin  1856 B  sha e932f708…
Dslr_Emnt_Shutter_Sha4.bin  1472 B  sha 0ef38673…
Dslr_Emnt_10FPS_Kasha*.bin   同上四套
Dslr_Emnt_Shutter_Ka.bin     128 B   sha 3cff4023…   ← 机械快门"咔"（极短）
```

**⇒ 相机有 4 套快门/连拍音效，不是一套。
我们只换了 `…Sha.bin` 和（原本打算的）`…Kasha.bin`。**

## 三、关键：谁决定用哪一套

`beeplist.txt` 里只有这两行指向 Sha/Kasha：

```
BEEP_ID_SHA    0x0002  PIN  Dslr_Emnt_Shutter_Sha.bin   KindID=2
BEEP_ID_KASHA  0x0003  PIN  Dslr_Emnt_10FPS_Kasha.bin   KindID=3
```

**而 `libObj.so` 里有这些字符串，说明运行时走的是 Backup 控制链：**

```
[M_BEEP] getBeepTypeFromBackup() ReadBackup Error:0x%X
[M_BEEP] getCustomBeepFromBackup() ReadBackup Error:0x%X
[M_BEEP] getBeepVolumeFromBackup() ReadBackup Error:0x%X
[[M_BEEP] setBeepTypeToBackup() BackupWrite Error:0x%X
[M_BEEP] setBeepType() Unknown Type 0x%X
[ObjSystemSound][SeqStartSysSndProcess] Warning:Not Suport CustomBeepID
[ObjSystemSound]GET PARAMID_CUSTOM_BEEP_LIST ERR [%x]
```

**⇒ 音效不是"开机从 /usr 读文件"，而是：**

```text
Backup（/setting/Backup.bin）里存 **beep type**（用哪一套）
    ↓
拍快门 → 读 beep type → 决定播放哪套音效 → 从 /usr 读对应 .bin
```

**⇒ 所以换 `…Sha.bin` 只对"beep type = 2"的情况有效。
如果 Backup 里 beep type 指的不是 2，就完全不生效 —— 播放的还是别的文件。**

## 四、为什么上游工具要"触发补丁"

**上游 README 说的"已安装相应电子快门声音补丁"，本质就是让 beep type 指到可替换的那一套。**
**而它要求固件 sha = `eef8b4fb…`，我们这台是 `bc6cba98…`（原厂）。**

**⇒ 上游工具**不提供、也不安装**那个补丁（README 原文：
"当前提供源码和原创合成示例，不发布 EXE；不附带 Sony 固件或 C2 原厂/实录声音素材"，
且"本工具不安装、升级或移除固件补丁"）。**

## 五、那还有没有办法？（三条）

### 路 1：改 Backup 里的 beep type（最小改动，但需要试）

```text
· /setting/Backup.bin 里应该有 beep type 的单元（对应 0x2b00xx 段）
· 我们已能离线解析、改字节、重算 checksum、写回（上一轮已验证可用）
· 需要先找出"beep type"那个单元号 —— 只能从相机上枚举 /setting 或反推
· 风险：改错单元可能影响别的设置；但有完整备份，可回滚
```

### 路 2：把 4 套音效全换掉（最直接）

```text
· 我可以把 Sha / Sha2 / Sha3 / Sha4 / Kasha1..4 / Ka 全部替换成你的声音
· 这样不管 beep type 指哪一套，听到的都是你的声音
· 代价：/usr 里这 9 个文件都变（体积净增约 200 KB，nflasha15 是 150 MB，够）
· 风险：低（每份都有备份；回滚 = 把 9 份写回）
```

### 路 3：改 beep type 的默认值（需要知道是哪个单元）

```text
· 与路 1 相同，但目标是把 type 固定成 2（指向我们换过的文件）
· 需要先能从相机读到当前 beep type 才能判断改哪个
```

## 六、我的建议

**先走路 2（换掉全部 9 个音效文件）**，理由是：

```text
① 它不依赖任何未知单元号，纯文件替换，我们已经验证过这套流程
② 无论相机用哪一套，出来的都是你的声音
③ 每份都有原厂备份，回滚简单
④ 若还是听不到，那就说明快门声根本不来自 avsystemsound（而在 DSP 侧），
   到时候再用「听不到」这个事实排除掉整条路，结论更硬
```

**但这台相机现在 Wi-Fi 又断了**（`10.42.0.158 No route to host`），所以需要你先把网络恢复。

## 七、如果路 2 也失败，说明什么

```text
· 说明快门声不是从 /usr/share/avsystemsound 播放的
· 那它可能在 /system/av-cam.bin（DSP 固件）那一侧，或 ADF（音频数据流）内嵌
· 这种就必须改 DSP 固件 —— 而 av-cam.bin 是 /system/nflasha3 上的 17 MB 镜像，
  改它有变砖风险（正是我们一直避免的）
```

**所以路 2 是"不改 DSP 前提下"的最后一次尝试。**

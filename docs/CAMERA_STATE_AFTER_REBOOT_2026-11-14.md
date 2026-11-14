# 相机重启后状态核查（2026-11-14 18:00）

## 一、最重要的事实：音效文件改动仍然存在

```text
/usr/share/avsystemsound/Dslr_Emnt_Shutter_Sha.bin
  = ffd1ff77f635d3904d11b0e9ca04440aef7aa5f911629dbc548e26923687b2d5
  = 我们替换的那份（24,192 字节）
```

**⇒ /usr 的改动没有被还原。**

## 二、但 /setting/Backup.bin 被相机重写了

```text
我写进去的： 5d68ccee5987869fa1dc…（音量改成 7）
现在相机里： 3cbb2fb5d6cf72df18b5…（相机自己重新生成的）
解析结果：0x2b0038 = 0，0x2b002a = 0（音量回到 0）
```

**⇒ 预期行为**：相机运行时会用自己的 Backup 机制重写该文件，
离线改的字节被覆盖。**音量必须走原生 Backup API 才能持久（需 USB 服务模式）。**

## 三、/tmp 空间问题已解决

```text
原因：/tmp 与 /etc 共用 /dev/nflasha10（12 MB），被历史探针残留占满
处置：删 /tmp/.zv10_n.* /tmp/.zv10_out.* /etc/leak.out* /etc/kikilog.dat
结果：/tmp 与 /etc 现在都可写 ✓
副作用：/etc/kikilog.dat（相机滚动日志 1.9 MB）会自行重建，无风险
相机 im.elf 仍在运行，系统健康
```

## 四、机制确认：beeplist 是音效选择的真入口

`/usr/share/avsystemsound/beeplist.txt` 里快门音效有多套：

```text
BEEP_ID_SHA        0x0002  Dslr_Emnt_Shutter_Sha.bin    KindID=2
BEEP_ID_KASHA      0x0003  Dslr_Emnt_10FPS_Kasha.bin    KindID=3
BEEP_ID_SHA_2      0x003D  Dslr_Emnt_Shutter_Sha2.bin   KindID=0xB
BEEP_ID_SHA_3      0x003E  Dslr_Emnt_Shutter_Sha3.bin   KindID=0xC
BEEP_ID_SHA_4      0x003F  Dslr_Emnt_Shutter_Sha4.bin   KindID=0xD
BEEP_ID_KASHA_2..4 0x0040..0x0042                        KindID=0xE..0x10
BEEP_ID_SPOT_KASHA 0x003C  20fps_KaSha.bin             KindID=8
```

`libObj.so` 里有三个 beeplist 路径，第一项在可写分区：

```text
/tmp/beeplist.txt                       ← 可写
/mnt/beeplist.txt                       ← 可写
/usr/share/avsystemsound/beeplist.txt   ← 只读分区
```

## 五、下一步

### 路 A（低风险）：覆盖 /tmp/beeplist.txt

```text
· /tmp 现在可写
· 复制 beeplist.txt 到 /tmp，把 BEEP_ID_SHA / BEEP_ID_KASHA 的 FileName
  指向我们自己的文件（如 myclick.bin）
· 把我们编码好的 24,192 字节放到 /usr/share/avsystemsound/myclick.bin
  （需临时 remount rw /usr，已验证可用）
风险：低（删 /tmp/beeplist.txt 即恢复）
不确定：/tmp/beeplist.txt 是否真被优先读取，需试一次
```

### 路 B（高风险，用户要求）：改 /system/av-cam.bin

**我已核查：av-cam.bin 里没有任何快门音效数据**

```text
· 完整 1216 B 音效：0 次出现
· 32 B 分块：0 次出现
· 128 B Ka 音效：0 次出现
· 只有 SystemSound 的 ADF 控制指令名（SetTable/SetPath/Request/SetVol…）
⇒ 它负责"播放控制"，音效数据在 /usr 里
⇒ 改它来换音效，方向本身不成立
```

**若仍要改：**
```text
① 先备份 17 MB 到 /log（500 MB 空间，够）
② 它是 MUSASHI DSP 的 ARM 代码，不是文本；改错会破坏拍照/录像链路
③ 相机对它有 sha 校验（上游工具写入前必核，说明固件会校验）
④ 最坏情况：相机不能拍照，但 im.elf 仍能起（agent 在 ld.so.preload，能救）
```

## 六、建议

**先走路 A**（成本几乎为零、可逆），失败再讨论路 B。
**但路 B 必须说清：音效数据不在 av-cam.bin 里，改它换不出你要的声音。**

# 快门音效：路 A 已在真机完成（2026-11-14）

## 一、做了什么

**方案：** 不改 av-cam.bin（已核实里面没有音效数据），改为
① 把 8 套快门/连拍音效文件全部替换成用户提供的声音
② 把 `beeplist.txt` 里所有指向其它音效变体的条目也重定向到同一份

**写入结果（全部经相机侧 sha256 校验）：**

```text
/usr/share/avsystemsound/
  Dslr_Emnt_Shutter_Sha.bin    ffd1ff77…  (24,192 B)  ← 新声音
  Dslr_Emnt_Shutter_Sha2.bin   ffd1ff77…  (24,192 B)  ← 同上
  Dslr_Emnt_Shutter_Sha3.bin   ffd1ff77…  (24,192 B)  ← 原 1,856 B
  Dslr_Emnt_Shutter_Sha4.bin   ffd1ff77…  (24,192 B)  ← 原 1,472 B
  Dslr_Emnt_10FPS_Kasha.bin     ffd1ff77…  (24,192 B)  ← 原 1,216 B
  Dslr_Emnt_10FPS_Kasha2.bin    ffd1ff77…  (24,192 B)
  Dslr_Emnt_10FPS_Kasha3.bin    ffd1ff77…  (24,192 B)  ← 原 1,856 B
  Dslr_Emnt_10FPS_Kasha4.bin    ffd1ff77…  (24,192 B)  ← 原 1,472 B
  beeplist.txt                  cba181f3…  (5,233 B)  ← 已重定向
```

**`/usr` 已恢复 ro；`im.elf` 仍在运行；相机未重启。**

## 二、全部原厂备份（在 /log，500 MB 分区）

```text
.log/.Dslr_Emnt_Shutter_Sha.bin.backup     1,216 B  957ae893…
.log/.soundlab-Dslr_Emnt_Shutter_Sha.bin.backup
.log/.soundlab-Dslr_Emnt_10FPS_Kasha.bin.backup
.log/.Dslr_Emnt_Shutter_Sha2.bin.orig      1,216 B
.log/.Dslr_Emnt_Shutter_Sha3.bin.orig      1,856 B
.log/.Dslr_Emnt_Shutter_Sha4.bin.orig      1,472 B
.log/.Dslr_Emnt_10FPS_Kasha.bin.orig       1,216 B
.log/.Dslr_Emnt_10FPS_Kasha2.bin.orig      1,216 B
.log/.Dslr_Emnt_10FPS_Kasha3.bin.orig      1,856 B
.log/.Dslr_Emnt_10FPS_Kasha4.bin.orig      1,472 B
.log/.beeplist.txt.orig                    5,239 B  c43866bb…
.log/.Backup.bin.orig                      1,235,836 B
```

## 三、beeplist 的改动细节

**原厂：**

```text
BEEP_ID_SHA       KindID=2   Dslr_Emnt_Shutter_Sha.bin
BEEP_ID_KASHA     KindID=3   Dslr_Emnt_10FPS_Kasha.bin
BEEP_ID_SHA_2     KindID=B   Dslr_Emnt_Shutter_Sha2.bin
BEEP_ID_SHA_3     KindID=C   Dslr_Emnt_Shutter_Sha3.bin
BEEP_ID_SHA_4     KindID=D   Dslr_Emnt_Shutter_Sha4.bin
BEEP_ID_KASHA_2..4 KindID=E..10  ...
```

**改后：** 全部指向 `Dslr_Emnt_Shutter_Sha.bin`（也就是我们的声音）。
**保留未动：** `BEEP_ID_SPOT_KASHA`（20fps_KaSha.bin，连续循环音，
其 `contLength=0x4E0` 语义与一次性音效不同，未替换）
以及所有非快门音效（菜单、对焦、录制、WiFi 等）。

## 四、为什么这样比改 av-cam.bin 靠谱

```text
· av-cam.bin 里**没有**任何音效数据（1216B/32B/128B 三种签名搜索均 0 次命中）
  ⇒ 它只有 SystemSound 的 ADF 控制指令（SetTable/SetPath/Request/SetVol）
  ⇒ 改它换不出声音，却有让相机不能拍照的真实风险
· 路 A 只动 /usr 里的资源文件 + 一份列表，全部有备份，回滚是复制文件
```

## 五、现在需要你做的（必须做，否则不生效）

```text
1. 关机
2. 取电池，等 10 秒，装回          ← 必须真实冷启动
   （相机当前 image_dev=*wbi4 / image_mode=2 ⇒ WBI 热启动会沿用旧 RAM 里的音效）
3. 开机
4. A 模式，确认「静音拍摄」开启（若已开，先关再开一次）
5. 按快门试听
```

**如果这次听到了：** 说明 avsystemsound + beeplist 就是正确链路，路 A 成功。
**如果还是原厂那声极短的"沙"：** 说明快门声不经这条链（在 DSP 侧或由 ADF 内嵌），
那时才需要考虑 av-cam.bin —— 但我会先告诉你这个结论，不会盲目改。

## 六、回滚（一条命令）

```sh
busybox cp /log/.beeplist.txt.orig /usr/share/avsystemsound/beeplist.txt
for f in Shutter_Sha2 Shutter_Sha3 Shutter_Sha4 10FPS_Kasha 10FPS_Kasha2 10FPS_Kasha3 10FPS_Kasha4; do
  busybox cp /log/.Dslr_Emnt_$f.bin.orig /usr/share/avsystemsound/Dslr_Emnt_$f.bin
done
busybox cp /log/.soundlab-Dslr_Emnt_Shutter_Sha.bin.backup \
    /usr/share/avsystemsound/Dslr_Emnt_Shutter_Sha.bin
```
（需先 `mount -o remount,rw /usr`，完事 `ro`）

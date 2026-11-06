# 快门音效：资源格式、加载路径、替换方法

## 一、资源位置与格式

```
目录: /usr/share/avsystemsound/           （v9 树里共 53 个 .bin，388 KB）
格式: 自研 8-bit ADPCM 流（非 WAV/MP3）
      文件头 4 字节后即 ADPCM 数据，例如：
        Shutter_Ka.bin        1344 B   17 00 00 01 01 03 …
        Shutter_Sha.bin       1344 B   07 00 00 00 00 04 …
        Dslr_Emnt_Shutter_Ka.bin  128 B 17 00 00 00 00 00 …
```
⚠️ **不是标准音频格式，需要按同格式编码才能替换**（不能直接丢一个 WAV 进去）。

## 二、播放路径（前端音效 vs 演出音效）

appFw 里有一张硬编码路径表（12 条，`/usr/share/avsystemsound/` 前缀）：

```
Shutter_Ka.bin        Shutter_Sha.bin      10FPS_Kasha.bin
20fps_KaSha.bin       Shutter_BOF_KaSha.bin FocusLock.bin
Rec_Start.bin         Rec_Stop.bin          SelfTimer.bin
SelfTimer_2sec.bin    Dsc_Startup.bin       Dsc_Shutdown.bin
```
**⇒ 这是"前端/菜单层"用的音效集（含快门 Ka/Sha）。**

另有 `Dslr_Emnt_*` 一整套（`Dslr_Emnt_Shutter_Ka/Sha/Sha2..4`、
`Dslr_Emnt_Rec_Start`、`Dslr_Emnt_Menu_Open_Option_On` …），
名称带 `Emnt`（element）后缀 ⇒ 属于**"我的菜单→音效设置"里可选的那套电子音**。

**⇒ 两套并存：前端固定音（Shutter_Ka/Sha）+ 用户可选音（Dslr_Emnt_*）。**

## 三、怎么改（三条路线，代价递增）

### 路线 A：直接替换同名文件（最简单）
```
把想替换的音效按**同格式**（8-bit ADPCM，头部字段一致）编码后，
覆盖 /usr/share/avsystemsound/ 下同名文件即可。
⚠️ 风险：格式不对会导致无声或杂音（固件不做格式校验，只会按字节解码）
⚠️ 空间：目录仅 388 KB，替换不改变大小则无空间问题
```
**推荐**：保持**字节长度完全相同**，只改数据段 —— 这样播放长度/缓冲都不变。

### 路线 B：改设置里的"快门音类型"
```
appFw 里有字符串 No508_ShutterSoundOffIcon（设置项的图标资源名）
⇒ 说明菜单里有"快门音：开/关"这一项
⇒ 若要换"音色"，需改设置项映射（在 string/uxc 资源里）
```
**这条路是"用户可选项"，不是替换音频文件。**

### 路线 C：自己解码/编码器
```
用 Python 写 ADPCM 编解码（8-bit 自研变体）：
  ① 从 Shutter_Ka.bin 解出 PCM
  ② 用同参数把新音效编码回去
  ③ 覆盖
⇒ 可控性最高，但需要先逆出 ADPCM 的具体预测器参数
```

## 四、建议的验证顺序（不改设备也能先看）

```
1. 先确认"哪个文件对应实拍快门声"：
   - 前端音：Shutter_Ka.bin（机械声）+ Shutter_Sha.bin（镜音）
   - 10FPS_Kasha / 20fps_KaSha：连拍音
2. 在 QEMU 里可以验证播放调用（hook appFw 的音频播放函数），
   但 QEMU 无音频后端 ⇒ 只能验证"被调用"，不能听
3. 真机上替换后立即能听 —— 这是最快验证方式
```

## 五、QEMU 侧能做的（有限）

```
✅ 可验证：哪个 .bin 在什么操作下被打开/读取（hook open/read）
✅ 可验证：播放函数被调用（hook appFw 的播放入口）
❌ 不可验证：音色是否正确（无音频设备）
```

## 六、真机操作（最直接）

```sh
# 备份
cp /usr/share/avsystemsound/Shutter_Ka.bin /tmp/Shutter_Ka.bin.bak
# 替换（保持字节数相同）
# ... 把你的同格式文件写回去 ...
# 无需要重启相机（下次播放即生效，视实现而定；不确定就重启）
```
⚠️ `/usr` 只读时需先 `mount -o remount,rw /usr`（若支持）。

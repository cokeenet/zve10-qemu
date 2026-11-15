# `ssbi_prof.bin` 追查结论：**固件包里也没有**

## 一、搜过的位置（全部为空）

| 位置 | 结果 |
|---|---|
| `/mnt/build/fw/COMPARE/extracted/ZVE10/`（解包树）| 无 `*ssbi*` |
| `.../extracted/ZVE10/rootfs/log/` | **空目录** |
| `.../extracted/ZVE10/usr/`（218 MB 全树）| 无 |
| `.../ZVE10/system/`（含 sabin/）| 无 |
| `/mnt/build/fw/unpacked/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha7_unpacked/log/` | **空目录** |
| `nflasha7_unpacked` 全树 `*ssbi*` / `*prof*.bin` | 无 |
| `nflasha15_unpacked`（/usr）全树 | 无 |
| 整个 `unpacked_zve10` | 无 |

**⇒ 结论：`ssbi_prof.bin` 不在固件包里，出厂时就不存在。**

## 二、这解释了一切

```text
· 内核参数 basic.prof_path=/log/ssbi_prof.bin
· /proc/ssboot/optimizer/basic/profname = /log/ssbi_prof.bin
· 但固件包 /log 是空目录，从未包含这个文件
⇒ **这个文件应该是「相机自己运行过程中生成」的**
⇒ 或者它是可选优化（缺失只是让优化失败，不影响基本启动）
```

### 关键判断

```text
从 dmesg：
  ssboot: failed to optimize image: -2
  （注意：**后面没有 "failed to create image"**）
⇒ 优化是**可选的**
⇒ 快照照样会被写下去，只是没优化
⇒ **所以缺 ssbi_prof.bin 本身不会导致砖**

⇒ 真正的砖，我认为是 warm.ignore_drop=1 那个被忽略的 drop 请求
   造成的内核/用户态状态不一致
```

## 三、★ 更重要的发现：固件包里有出厂 WBI

```text
/mnt/build/fw/unpacked/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha5
  大小：36,039,424 B（34 MB）
  头部：57 42 49 31 98 02 00 00 09 00 00 00 1c 4e 12 00   （"WBI1" 开头）
```

**相机上的当前 WBI 头部与固件包**逐字节完全一致**：**

```text
固件包  : 57 42 49 31 98 02 00 00 09 00 00 00 1c 4e 12 00
相机    : 57 42 49 31 98 02 00 00 09 00 00 00 1c 4e 12 00
          （+0x10: 24 02 06 20 00 02 00 00 00 98 21 02 00 00 00 00  也一致）
```

**⇒ 说明相机上的 WBI 头部就是出厂的那个**（说明相机实际上**没有成功更新过快照**，
   或者说更新后的头部与出厂相同 —— 大概率是前者：优化失败所以根本没写新快照）。

## 四、这对「清 WBI」方案意味着什么

```text
★ 好消息：我们有**出厂 WBI**（36 MB，在固件包里）
   ⇒ 如果清空后砖了，可以把出厂 WBI dd 回去
   ⇒ 比"只能 dd 回自己抓的备份"更可靠（这是索尼原厂镜像）

★ 但大小不同：固件包 36 MB，相机分区 60 MB
   ⇒ 相机分区比固件包里的镜像大
   ⇒ dd 回去时只需写前 36 MB，后面留空即可（或按原样）

★ 最激进但最安全的做法：
   直接把**出厂 WBI dd 进相机分区** ⇒ 相机回到出厂快照状态
   （而不是清空它）
```

## 五、修正后的建议（比之前更安全）

```text
方案 A（最安全，推荐）：把出厂 WBI 写回相机
  dd if=<固件包nflasha5> of=/dev/nflasha5
  ⇒ 相机 WBI 变回出厂状态
  ⇒ 若再重启，内核会加载出厂快照 ⇒ **应该能正常启动**
  ⇒ 这就是「恢复出厂快照」，不是「清空」

方案 B（你要的清空）：dd if=/dev/zero of=/dev/nflasha5
  ⇒ 内核发现无有效快照 ⇒ 走冷启动
  ⇒ 风险：冷启动路径可能没被验证过

★ 我建议先做 A（不是清空，而是写回出厂镜像）
  因为它用的是索尼自己的数据，最不容易出问题
```

**⇒ 而且方案 A 还附带一个效果：如果出厂快照是「真正冷启动后的状态」，
   那么恢复它之后 bootin 会重新执行 ⇒ 就能重读 dmode/preload ⇒ 达到我们的目标！**

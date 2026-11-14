# 改 av-cam.bin 之前必须先说清的三件事

## 一、上游确实不给补丁

**README 原文两句：**

```
"本工具不安装、升级或移除固件补丁。"
"当前提供源码和原创合成示例，不发布 EXE；不附带 Sony 固件或 C2 原厂/实录声音素材。"
```

**它只接受固件 sha = `eef8b4fb…`（已打补丁）；我们这台是 `bc6cba98…`（原厂）。**
**⇒ 那个补丁在市面拿不到。你要求改 av-cam.bin，这是唯一剩下的路。**

---

## 二、⚠ av-cam.bin 到底是什么（先说风险）

**我用本地固件树摸清了它：**

```
文件：/system/av-cam.bin        17,289,388 字节
sha256：bc6cba98c1ce5ef9c7692b9701ed855c25958d285a646ef90dc3fdf398f5c4fa（= 相机当前值）
前 16 字节：0a 00 00 ea  4f 52 49 4c  04 00 00 00  44 60 5c 63
  ⇒ 开头是 ARM 分支指令（0a 00 00 ea = b +8），随后 "ORIL" 魔数
  ⇒ **不是 ELF，是厂商自有容器**
落在：/dev/nflasha3（vfat，48 MB，**rw**）
运行：MUSASHI 协处理器固件，由 liro.ko 加载到 0x635c6000
```

**它是 DSP/协处理器固件，不是普通配置文件。改它有四个具体风险：**

```
① 没有校验绕过手段：相机可能对 av-cam.bin 做完整性验证，
   改了但签名/CRC 不对 ⇒ 可能拒绝加载 ⇒ 拍照/录像功能失效
② 它是拍照链路的核心（AF、曝光、编码都在这里）
   改坏 ⇒ 不只是声音没了，是相机不能拍照
③ 它不是"音效文件"，里面**没有**任何快门音效数据（我搜索过：
   1216 B 完整音效 0 次出现，32 B 分块 0 次出现，128 B Ka 0 次出现）
   ⇒ 改它来换音效，方向本身可能就是错的
④ 我们之前从相机侧看到：av-cam.bin 被 av-cam 校验守卫
   （上游工具在写任何东西前会先核它的 sha）
```

---

## 三、但我发现了**更对**的一条路：`/tmp/beeplist.txt`

**`libObj.so` 里有三个 beeplist 路径，优先顺序可疑：**

```
ef8f4c  /tmp/beeplist.txt                      ← **可写！**
ef8f5e  /mnt/beeplist.txt                       ← 可写（若有 mnt）
ef8f70  /usr/share/avsystemsound/beeplist.txt   ← 只读分区
```

**⇒ 固件按顺序尝试这三个路径。`/tmp/beeplist.txt` 排在第一位。**

**这意味着：**

```
放入 /tmp/beeplist.txt（我们自己的版本）
  ⇒ 相机优先读它
  ⇒ 在列表里把 BEEP_ID_SHA / BEEP_ID_KASHA 指向我们自己的文件
  ⇒ **完全不用改 /usr，也不用改 av-cam.bin**
```

**但有个问题：这台机器的 `/tmp` 已满（nflasha10 = 12 MB，0 剩余）。**

⇒ 两条子路：
- **子路 A**：先腾出 /tmp 空间（删掉那些 `.zv10_n.*` 垃圾，约 40 个小文件 + `kikilog.dat` 1.9 MB 等），然后写 /tmp/beeplist.txt
- **子路 B**：`/log` 有 500 MB，若固件支持 `/mnt/` 或能 bind，就用 `/mnt/beeplist.txt`

---

## 四、我的建议（按风险排序）

```
① 先走 beeplist 覆盖（改 /tmp 或 /mnt，**不碰 /usr、不碰 /system**）
   · 风险：极低（写坏只是音效没了，删掉文件即恢复原厂）
   · 需要：/tmp 腾空间，或确认 /mnt 可用
② 若 ① 不行，再考虑改 av-cam.bin（你要求的那条）
   · 风险：高（可能让相机不能拍照）
   · 前提：先完整备份 av-cam.bin 到 /log（17 MB，/log 有 500 MB，够）
③ 无论哪条，都要冷启动
   · 但这台 machine 的 image_dev=*wbi4 + image_mode=2 ⇒ 走 WBI 热启动
   · 我已实测把 image_mode 改为 0（丢弃快照），但 /proc/ssboot/state 仍是 snapshot
   · **必须取电池才能保证真实冷启动**（热启动会沿用旧 RAM 里的音效）
```

---

## 五、需要你确认的两件事

```
Q1：你接受"改 av-cam.bin 可能让相机不能拍照"这个风险吗？
    （有完整备份，理论上可恢复，但恢复需要 shell 还在 —— 
     若 UI 起不来，agent 仍会加载（它在 /etc/ld.so.preload），所以能救）

Q2：或者先让我试 beeplist 覆盖那条低风险的路？
```

**我的专业意见：先试 beeplist（成本几乎为零），不行再动 av-cam.bin。
但你要直接改 av-cam.bin，我也会做 —— 前提是先把 17 MB 完整备份到 /log。**

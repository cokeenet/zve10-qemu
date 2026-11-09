# 通过 agent 执行路线 A 和路线 C 的可行性评估

相机当前不在线（`10.42.0.158:7777` 无响应，USB 无 `054c`/`18d1`）。以下结论全部基于本地一手数据。

---

## 路线 A：离线改 4 字节 WBI 标记

### 能不能通过 agent 做？

**不需要 agent。** A 是**离线**操作：改本地备份文件，再用 fastboot 写回。

**但前提是能进 fastboot**（BootROM + Pico 注入），不是 agent 能做到的。

### 关键新发现：正确的无效标记是 `invl`，不是随便编一个

从 Loader2 反汇编（VA `0xFE047000`）拿到四个谓词：

```
0xFE049CCE  is_invl   → fVersion == "invl"
0xFE049CF0  is_prof   → fVersion == "prof"
0xFE049D10  is_WBI1   → fVersion == "WBI1"
0xFE049D30  is_WBI0   → fVersion == "WBI0"
```

而 Loader2 的命令表（`0xFE055560` 起，每项 `0x20` 字节）显示这些谓词挂在**启动模式处理器**上：

```
0xFE0555C0  bis_1st   handler=0xFE049A0B   → is_invl
0xFE0555E0  bis_2nd   handler=0xFE049A19   → is_prof
0xFE055820  ssboot    handler=0xFE049A27   → is_WBI1
0xFE055840  ss_bis    handler=0xFE049A35   → is_WBI0
0xFE055860  cldboot   handler=0xFE049A43   → 无条件返回 0（冷启动）
0xFE055880  error     handler=0xFE049A43
```

**⇒ `cldboot` 的处理器 `0xFE049A43` 就是 `movs r0,#0; bx lr`（无条件"不恢复"）。**

**⇒ 而 `is_invl` 对应 `bis_1st` —— 内核 `warm.c:1131` 在写镜像前先把 `fVersion` 写成 `"invl"`，Loader2 明确认得这个标记。**

**所以路线 A 应该写 `invl`，不是 `WBI9`：**

```python
data = bytearray(backup_nflasha5)      # 62,914,560 B
assert data[0:4] == b'WBI1'
data[0:4] = b'invl'                    # 内核自己用的"无效"标记
```

**为什么 `invl` 比自造标记好：**

| 标记 | Loader2 行为 |
|---|---|
| `invl` | 命中 `is_invl` → `bis_1st` 分支 → **官方定义的"无效"路径** |
| `WBI9`（自造） | 四个谓词全不命中 → 行为**未定义**（我不知道调用方怎么处理全 false） |
| `\0\0\0\0`（我做的） | 同上，而且 `sector_size=0` 让参数自相矛盾 → **实测卡死** |

**⇒ `invl` 是唯一有源码依据的"让 WBI 失效"标记。**

### 写回怎么算

**只改 4 字节，但 `flash:` 只能整分区从 0 开始写。用 sparse 只写第 0 块（4096 B）：**

```python
blk = bytearray(original_nflasha5[0:4096])
blk[0:4] = b'invl'
# 其余 4092 字节保持原样（含 sector_size=512、sections=664、r_data_size）
```

**工具链已就绪：**

```
/root/fw/oss/fastboot/host/sparse_block.py   --part nflasha5 --offset 0 --data blk.bin --yes
```

**它构造 `SKIP 0 | RAW 1 block | SKIP rest` 的 sparse 镜像，只写 1 个 4096 字节块。**

---

## 路线 C：改 cmdline `warm.mode=0`

### 能不能通过 agent 做？

**理论上可以（改 `/setting/kemco.txt` 一个字节），但有两个硬问题。**

### 问题一：`/setting/kemco.txt` 可能不是 loader 读的那份

**本地实测的差异：**

```
真机 /proc/cmdline（a_p0/01_cmdline.txt，一手）    90 tokens
nflasha1:/BOOT/KEMCO.TXT（1062 B）                 59 tokens，交集仅 34
nflasha2:/kemco.txt（1925 B）                      98 tokens，交集 89
```

**⇒ 真机 cmdline 与 **nflasha2 的 kemco.txt** 高度吻合（89/90），与 nflasha1 的 `/BOOT/KEMCO.TXT` 只有 34 个相同。**

**⇒ 但 Loader2 的字符串里明确是 `\boot\kemco.txt`，而且它挂载的是 "Updater Partition"。**

**⇒ 两者矛盾。可能解释：**

1. **`nflasha1` 的那份是**旧固件残留**，loader 实际从别处读**
2. **`nflasha1` 的 `/BOOT` 只是 fallback，真源在 `nflasha2`（通过某条我没追到的路径）**
3. **两份都不是，真源在 `nflasha3` 或 boot 区，我还没找到**

**⇒ 在没搞清"哪份文件决定 cmdline"之前，改 `/setting/kemco.txt` **不保证生效**。**

**⇒ 这一条必须先在真机上只读验证：改之前备份，改之后 `cat /proc/cmdline` 看 `warm.mode` 是否变 0。**

### 问题二：只做 C 不够

**我做过推演：**

```
只做 A（改标记）：这次冷启动了
                → 但关机时 image_mode=2，内核把 fVersion 又写回 WBI1
                → 下次启动又是热恢复

只做 C（mode=0）：关机不重建
                → 但 flash 上 WBI1 还在
                → 下次启动还是热恢复
```

**⇒ **A 和 C 必须一起做。** A 负责"这一次"走冷启动，C 负责"以后每次"都保持不重建。**

### 问题三：C 的写入是持久的，且会影响相机正常行为

**`warm.mode=0` 的连锁效果（内核源码）：**

```
image_mode = 0
→ cxd_create_warmbootimage() 返回 0，关机不写镜像     【想要】
→ _wbi_drop() 提前返回，任何 drop 都无效              【附带】
→ wbi_resume_drop() 提前返回                         【附带】
→ warm_boot_pm_prepare() 提前返回                    【附带】
```

**⇒ 相机会**永久失去快速启动**，每次开机都是完整冷启动（更慢、更耗电）。**

**⇒ 这是**行为改变**，不是 bug 修复。用户需要知道并接受。**

---

## 我的判断：**A 可以做，C 现在不该做**

### A 可行（推荐先做）

**理由：**

- 4 字节改动，其余 4092 字节原样保留
- 有 `invl` 这个官方标记做依据
- 可随时改回 `WBI1`
- 不改变相机任何运行时行为，只影响"这次启动是否恢复快照"

**执行步骤：**

```
1. 相机进 fastboot（Pico 注入 stage1 + stage2）
2. 生成 blk.bin：原 nflasha5[0:4096]，[0:4]='invl'
3. sparse_block.py --part nflasha5 --offset 0 --data blk.bin --yes
4. 回读验证前 4096 字节
5. 拔电池 ≥1 分钟，装回开机
6. 验证：cat /proc/sys/kernel/random/boot_id（应与上次不同）
```

### C 暂缓（需要先验证一件事）

**必须先回答：** `/setting/kemco.txt` 到底是不是 loader 读的那份？**

**这个问题只能**在真机上、相机正常运行时**验证：**

```sh
# 只读
cat /proc/cmdline | tr ' ' '\n' | grep warm.mode
# 然后（需授权）改 1 字节，重启，再看
```

**在没验证之前改 C，等于**蒙**。而且 C 会永久改变启动行为。**

---

## 更重要的：**A 和 C 都不是"让菜单出现"的直接手段**

**回到原始目标。**A/C 只解决"改动为什么不生效"这个**前置障碍**，并不产出菜单项。

**真正的交付链仍然是：**

```
冷启动生效（A/C 解决）
  → /etc/ld.so.preload 被 ld.so 读取
    → libnewmenu 加载进 im.elf
      → nm_try_install 装 hook
        → ne_init_wrapper 插入 New Menu 行
          → ENTER 进入 A7C2 菜单
```

**QEMU 侧这条链已经全程验证过**（路线 A 三帧哈希一致 + 延迟插入完整通过）。

**真机侧缺的只有第一步"冷启动"。**

---

## 建议的行动顺序

| 步骤 | 手段 | 风险 |
|---|---|---|
| 1. 只读确认当前 `nflasha5` 头部完好（`WBI1`） | agent 或 fastboot 回读 | 无 |
| 2. 生成 `invl` 块，离线校验 | 本地 | 无 |
| 3. fastboot 写 4096 字节 | 需 Pico + 授权 | 低（只改 4 字节） |
| 4. 拔电池冷启动，验证 `boot_id` 变化 | 手动 | 低 |
| 5. 验证 `im.elf` 加载了 `libnewmenu` | agent | 无 |
| 6. **此时才验证 C 是否必要** —— 如果一次冷启动后改动生效，可能根本不需要 C | agent | 无 |

**⇒ 第 6 步是关键：如果只做 A 就能让改动生效，C 就不必做。**

**⇒ 但如果相机之后正常关机（image_mode=2），内核会重建 `WBI1`，下次又是热恢复。所以 C 迟早要做，除非能接受"每次改完必须立刻拔电池"。**

---

## 需要你决定的

1. **先做 A（低风险，可回滚）还是等 C 的可行性验证完再做？**
2. **是否接受 `warm.mode=0` 带来的"永久失去快速启动"？**

**相机现在不在线，需要你把它接回来（联网 + agent 运行）或者进 fastboot。**

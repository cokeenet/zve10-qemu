# ★ WBI 快照机制全貌（本轮从内核接口读出来的第一手资料）

## 一、内核接口 `/proc/ssboot/`

```text
/proc/ssboot/
├── mode          内容: optimize
├── operation     内容: normal
├── resmode       内容: normal
├── state         内容: snapshot
├── swapfile      内容: null
├── optimizer/
│   ├── operation 内容: stopped
│   └── basic/
│       ├── estimation   内容: (空)
│       └── profname     内容: **/log/ssbi_prof.bin**     ← ★★★ 内核自己说的
├── region/
│   ├── critical 表头: Physical Address KByte Pages Attribute  (无数据行)
│   ├── discard  表头: Physical Address KByte Pages Attribute  (无数据行)
│   ├── normal   表头: Physical Address KByte Pages Attribute  (无数据行)
│   ├── work     表头: Physical Address KByte Pages Attribute  (无数据行)
│   ├── init     (空)
│   └── remove   (空)
└── section      不存在
```

## 二、与 dmesg 的对应关系（完整链条）

```text
[1148.78] Stopping user tasks ... done.
[1150.08] ssboot: optimizing image...
[1150.08] ssboot: cannot open file: /log/ssbi_prof.bin
[1150.08] ssboot: failed ssboot_basic_read_profile() -2
[1150.08] ssboot: failed to optimize image: -2
[1150.08] Restarting tasks ... done.

⇒ 内核配置 profname = /log/ssbi_prof.bin（来自 /proc/ssboot/optimizer/basic/profname）
⇒ 该文件不存在 ⇒ read_profile 返回 -ENOENT(-2)
⇒ 优化失败 ⇒ 快照没被优化（但**快照本身还是写下去了**，只是没优化）
⇒ optimizer/operation = stopped（优化器处于停止状态）
```

## 三、WBI 分区本身（nflasha5）是可读的

```text
/dev/nflasha5  存在，dd 可读（1 MB 读取成功，11.7 MB/s）
前 64 字节解析：
  +0x00  57 42 49 31         magic = "WBI1"
  +0x04  0x00000298 (664)     可能是 header/entry 大小
  +0x08  0x00000009 (9)       可能 entry 数
  +0x0c  0x00124e1c (1,199,644)  ≈ 1.14 MB（内存镜像大小？）
  +0x10  0x20060224           时间戳风格（0x2006-02-24?）
  +0x14  0x00000200 (512)     对齐/块大小
  +0x18  0x02219800 (35,756,032) ≈ 34 MB
  +0x24  0x0f0f0f0f            填充模式
  +0x28  0x08234000 (136,527,872) ≈ 130 MB
```

**⇒ WBI 分区有内容，"WBI1" 格式头完整。**

## 四、★ 关于「清 WBI 能不能让实验成功」的判断

### 事实层面

```text
· 清空 WBI（nflasha5）⇒ 下次开机没有快照可恢复 ⇒ **必须走完整冷启动**
· 但当前 image_mode=2、image_dev=*wbi4、ssboot state=snapshot
  ⇒ 这些是内核/驱动侧的状态，清分区不会自动改它们
· 冷启动本身**不一定失败**：之前的"砖"是因为快照**残缺**（优化失败），
  而不是因为冷启动这条路本身有问题
```

### 关键区别（这是本轮最重要的判断）

```text
情况 A：drop 快照 + 冷启动（你之前试过 ⇒ 砖）
        ⇒ 因为冷启动时内核会尝试加载那个**残缺快照**，加载失败就卡死

情况 B：彻底清空 WBI 分区 + 冷启动
        ⇒ 没有快照可加载 ⇒ 内核走**纯冷启动路径**（不需要快照）
        ⇒ **理论上比 A 更安全，因为不给它加载残缺数据的机会**
```

**⇒ 你的想法是对的：清 WBI 比 drop 更彻底、更干净。**

### 但我必须先说清三个风险

```text
风险 1：清空后若冷启动仍然依赖 WBI（例如 kernel 命令行指向 wbi4）
        ⇒ 读到空分区可能直接 panic
风险 2：清空后无法回退（除非先把 60MB 全备份出来）
风险 3：我们并没有"救砖"的确定手段（Pico fastboot 需要硬件）
```

### 因此：如果要试，必须先做三件事

```text
1. **完整备份 nflasha5 分区**（约 60MB，dd 到 /log 或 SD 卡）
   —— 已知 dd 可读，60MB 需要约 6 秒
2. **先验证"清零某个小块"能不能开机**（不做全清）
   —— 例如只清零头部 64KB，看是否仍能启动
3. 备份后如果真的砖了，至少可以把分区原样 dd 回去（如果有办法写入）
```

## 五、更安全的替代方案（我推荐先做这个）

```text
★ 与其清 WBI，不如**补上 ssbi_prof.bin**！

理由：
· 内核明确说它要 /log/ssbi_prof.bin
· 如果这个文件存在且格式正确 ⇒ 优化成功 ⇒ 快照变完整
· 完整的快照 ⇒ drop / 冷启动都安全
· **完全不需要碰 WBI 分区**

难点：这个文件格式未知（没有样本）
      ⇒ 但可以从 WBI 头部的字段推测，或从 kernel 里读 ssboot 代码
```

## 六、下一步建议

```text
第一步（零风险）：完整备份 nflasha5 到 /log（60MB，我们刚清空 /log 有空间）
第二步（低风险）：反汇编内核 ssboot 模块，搞清楚 ssbi_prof.bin 的格式
第三步（中风险）：构造一个合法的 ssbi_prof.bin，让优化成功
第四步（最后手段）：若前三步不行，再考虑清 WBI
```

# Loader magic-check bypass + backup_db generators + kernel/loader patches

> 来自另一个独立复现线的 ZV-1F 新菜单移植工作（自建 QEMU + 自建 eMMC 镜像）。
> 本 PR 只提供**可复用的工具与二进制补丁**，不修改构建脚本、不引入我们的实验流水线。
>
> ⚠️ 先说明性质：下面第 1 节的装载器补丁是**绕过（bypass）**检查，不是修复根因。
> 它能让装载器在 backup 数据不可用时继续走到空列表路径 —— 适合用来后移崩点、排查下游，
> **不适合**当作「装载器已修好」的结论使用。

---

## TL;DR

| # | 内容 | 性质 |
|---|---|---|
| 1 | 6 个二进制补丁（libObj / backup.ko / stream.ko） | 绕过 + 一处真修复（VM_PFNMAP） |
| 2 | `make_backup_db.py`：最小 DB 布局生成器（3 条记录，完全自包含） | 工具 |
| 3 | `make_backup_db_v3.py` + `requested_ids.txt`：全量 258 条 DB（253 零值 + 5 真实） | 工具 |
| 4 | `check_db.py`：DB 结构校验（实现 `Data_SearchId` 算法） | 工具 |
| 5 | `check_fontlist.sh`：核对运行时 fontlist.dat 与源树是否一致 | 只读检查 |

所有补丁均带**文件字节序指纹校验 + 回读校验**，指纹不匹配直接 assert 失败，不会误写。

---

## 一、装载器三道 magic —— 这是 bypass，不是修复

libObj 装载器 `0x206d04` 的控制流（我们反汇编的完整版）：

```
0x206d2c  blx Backup_get_datasize
0x206d54  BIN! @buf[0]   检查失败 → r4 = -1
0x206d72  count_A = buf[8]
0x206d78  count_B = buf[12]
0x206d82  CNF! @buf[16]  检查失败 → r4 = -8
0x206e9c  PTN! @buf[20]  检查失败 → r4 = -9   ★ 在循环之后，容易漏
```

三道全过才继续 → `new[](count_A*16)` + `new[](count_B*52)` + sentinel(8)。

**我们做的事**：把这三处 `beq` 改成 `b`，并把 `count_A/count_B` 强制为 0
（count=0 = 空列表，是合法路径）：

| 偏移 | 原始 | 补丁 | 效果 |
|---|---|---|---|
| `0x206d72` | `ldr r6,[r5,#8]` | `movs r6,#0` | count_A = 0 |
| `0x206d78` | `ldr r3,[r5,#12]` | `movs r3,#0` | count_B = 0 |
| `0x206d82` | `beq.n` | `b.n` | CNF! 检查失效 |
| `0x206e9c` | `beq.n` | `b.n` | PTN! 检查失效 |
| `0x206d2c` | `blx get_datasize` | `movs r0,#28; nop` | size 钉死 28（DISCRIM） |
| `0x206d54` | `bne.w`（BIN! 失败跳转） | `nop; nop` | BIN! 检查失效 |

**实测结果**（我们的 QEMU，四轮一致）：`DMP_2D_Initialize : 0`，
App 能继续走到 appFw / stella 初始化。**但这是绕过状态下得到的**，不代表 backup 通路已通。

### 顺手踩到的坑（供参考）

`bf 00 bf 00` **不是** NOP —— 它是两条 `lsls r7,r7,#2`，会把帧指针左移 4 位。
正确写法是 `00 bf 00 bf`。我们因为这个问题浪费了一轮。
`patch_libobj_discrim.py` 里的指纹全部是**从文件自身读出的字节序**，不是从 objdump 反汇编列抄的。

---

## 二、backup_db 两个生成器（用途不同，别混）

### `make_backup_db.py` —— 最小布局，完全自包含

```bash
$ python3 make_backup_db.py out.bin
wrote out.bin  3183 bytes (0xc6f)
N1=0x171  pages: 0x112=(count2,cum0) 0x170=(count1,cum2)
slots@0x9a6  data@0x9b8  set@0x9b8  bin@0x9bc
checksum=0x0001b656
```

产出 **3 条记录**（`0x01120001` BIN! 28B 四 magic、`0x01700000` SET 4B、`0x0051000E` ulog）。
够用来验证「装载器能不能走到 CNF!/PTN! 检查」，不需要任何外部输入。

### `make_backup_db_v3.py` —— 全量 258 条

```bash
$ python3 make_backup_db_v3.py                      # 用同目录 requested_ids.txt
来源: id 清单 .../requested_ids.txt
原始 253 个, 过滤后 253 个 (丢弃 0 个池残影)
总记录 258 条
wrote backup_db_v3.bin  30325 bytes (0x7675)  N1=0x2bb  slots=4133
自检: 258 OK / 0 FAIL
```

258 = **253 个 App 实际请求过的 id（补成 4 字节零记录）+ 5 条真实记录**。
不是「全部 258 个 id 都命中」—— 253 个是我们喂过的，覆盖的是那一次运行的请求集合。

`requested_ids.txt` 是从一次 GDB 抓包里提取的清单，**已随本 PR 提交**，
所以脚本自包含；也可以用 `--ids-file` 换自己的清单，或 `--gdb-log` 从日志提取。

### `check_db.py` —— 结构校验

实现 `Data_SearchId` 的查找逻辑（从 backup.ko `0x31fc` 反汇编得到）：

```
page = id >> 16;  sub = id & 0xffff
页表[page] → {count, cum}（6B/页，头 0x100 起）
槽表[cum + sub] → v = (type << 24) | off
v == 0xFFFFFFFF → 空槽（返回 -1，是正确行为，不是错误）
```

---

## 三、TextRM fontlist：我们**推翻了自己早期的结论**

这一节值得单独说，因为我们第一版判断错了。

### 代码路径（libObj `0x8fc2c`）

```
0x8fc62   cbnz r5, 0x8fc74     ← buf 非空则走「主路径」
主路径:   fopen(buf)            buf = Backup_read(0x00810000) 的内容
fallback: fopen("TEXTRM_FONT_LIST", "r")   ← 字面量文件名，不是 getenv！
```

### 我们最初的说法（**错的**）

> 在候选 cwd 创建 `TEXTRM_FONT_LIST` 文件 → abort 消失

实测这个做法确实让 InfraIndctr 进程过了 TextRM，所以我们当时以为找到了修法。
**但后续两个实验推翻了它：**

| 实验 | 做法 | 结果 |
|---|---|---|
| 4a | 把字面量改成绝对路径 `/usr/fl.dat`（运行时确认 360 B 存在） | **abort 依旧** ⇒ cwd 不是原因 |
| 4b | `0x8fc62 cbnz` → `nop`，强制走 fallback | **SIGABRT 消失** ⇒ 主路径才是元凶 |

### 真实结论

`Backup_read(0x00810000)` 返回了正值，但**内容不是可用路径**，
于是主路径 `fopen(垃圾字符串)` 失败，而代码**失败后不重试 fallback** → 断言。
InfraIndctr 能过，是因为它对同一个 id 得到 `<=0`，自然落进了 fallback。
**两个进程的差异在这里**，不是环境问题。

⇒ 「在 cwd 建文件」只是恰好帮 InfraIndctr 绕了一圈，不是修法，对 AppFw 无效。
真正的修法要么让 `Backup_read` 返回正确的字体列表路径内容，要么补「主路径失败后重试 fallback」。
本 PR **不提供**这一修法（尚未验证），只记录判别实验的方法。

`check_fontlist.sh` 是个只读工具，用来排除「运行时文件被改坏」这种可能：

```bash
./check_fontlist.sh <源树 fontlist.dat> <usr ext2 镜像>
# ✅ 运行时文件与源树一致  /  ❌ 不一致 + diff
```

---

## 四、补丁清单

| 补丁 | 做什么 | 性质 |
|---|---|---|
| `patch_libobj_discrim.py` | get_datasize 钉死 28 + BIN! 放行 | 绕过 |
| `patch_libobj_bypass_cnf.py` | CNF!+PTN! 放行 + count 归零 | 绕过 |
| `patch_libobj_dtor_zero.py` | 析构 `this+0x0C/+0x10` 置零 | 绕过（抑制 free 垃圾指针） |
| `patch_libobj_dtor_zero2.py` | 兄弟类 `0x205d84` 析构置零 | 绕过 |
| `patch_stream_pfn.py` | `stream_mmap` 的 `bic r3,r3,#0x4400` → NOP | **真修复** |
| `patch_backuposal.py` | `backup_init` → `b backup_osal_init`（强制注册 qid 回调） | 环境补丁 |

`patch_stream_pfn.py` 是唯一性质上的真修复：
`stream_mmap()` 在 `remap_pfn_range()` 后清掉 `VM_PFNMAP|VM_IO`，
导致 `munmap` 时 `vm_normal_page()` 返回 struct page → 打穿 mapcount → `Bad page map`。
保留 `VM_PFNMAP` 后我们的 `Bad page map` 从 60 条降到 **0**。

---

## 五、与本项目已有工作的关系

| 方向 | 本项目 | 我们 |
|---|---|---|
| DMM 协议 RE | ✅ 完整契约 + 实测生效 | 未做 |
| grm_ma 分派表 | ✅ opcode 1-13 全解码 | 未做 |
| 真机取证 | ✅ 完整 maps / stream 窗口 | 无相机 |
| 装载器 magic 检查 | 未涉及 | ✅ 三道全定位 + bypass |
| backup_db 生成 | 未涉及 | ✅ 两个生成器 + 校验器 |
| VM_PFNMAP / Bad page map | 未涉及 | ✅ 根治（60→0） |

**互补**：你们有真机数据和 DMM 契约，我们有 backup 侧的完整工具链。

---

## 六、已知待解（我们没解决的）

1. `Backup_read(0x00810000)` 返回正值但内容不可用 —— 传输层协议本身是通的
   （我们已用内核侧 GDB 确认每个请求都到 handler、回复也写回用户 msg），
   问题在「返回什么」而不是「回不回」。
2. AppFw / InfraIndctr 在 `raise(SIGABRT)` 处报 illegal instruction。
   **已排除 Thumb 状态丢失**：`libc+0x15790` 是 `push{r7}; mov r7,ip; svc #0; pop{r7}`，
   PC 报的是 fault+2，cpsr 的 T 位正常 ⇒ 判断为 glibc SVC 存根在 QEMU 下的边界假象。
3. `stella HeapAllocator((nil))` 池基址为 nil，后续分配未验证。
4. 我们自己的实验环境曾污染过解包树（见下），相关归因已撤回。

### 一条方法论提醒

> 我们踩过一个坑：**把补丁打进了金标准解包树**，而构建脚本每次都从那棵树拷贝。
> 结果「对照组」一直带着补丁，导致几个归因结论失效（`dtor_zero` / UIPC `lazy2` 均受影响）。
> 建议本项目也在构建前加一道闸门：**拷完 SRC→TREE 立刻校验所有补丁点必须是原始值**。

---

## 七、怎么用

```bash
# --- 装载器补丁（按顺序，打在你的 usr 镜像树里的 libObj.so 上）---
python3 patches/patch_libobj_discrim.py    /path/to/libObj.so
python3 patches/patch_libobj_bypass_cnf.py /path/to/libObj.so
python3 patches/patch_libobj_dtor_zero.py  /path/to/libObj.so
python3 patches/patch_libobj_dtor_zero2.py /path/to/libObj.so

# --- 内核模块补丁 ---
python3 patches/patch_stream_pfn.py        /path/to/kmod/      # 需含 stream.ko（有 stream2.ko 就打两个）
python3 patches/patch_backuposal.py        /path/to/kmod/backup.ko

# --- backup_db ---
python3 tools/make_backup_db.py    small_db.bin            # 最小版：3 条
python3 tools/make_backup_db_v3.py full_db.bin             # 全量版：258 条，用同目录 requested_ids.txt
python3 tools/check_db.py          full_db.bin              # 结构校验

# --- fontlist 一致性检查（只读）---
bash tools/check_fontlist.sh <源树 fontlist.dat> <usr ext2 镜像>
```

所有脚本都支持无参数时打印用法；没有参数时**不会**猜目录、不会改任何文件。

---

## 八、这些结论的适用范围（重要）

我们的环境是**自建 QEMU（cxd90045 + ZV-1F 固件解包）**，没有真机。
所以：

- ✅ 补丁的**字节偏移与指纹**是从真实固件文件读出的，可直接复用
- ✅ `Data_SearchId` 算法是从 `backup.ko` 反汇编复现的
- ⚠️ 所有「崩点后移 / 断言消失」的观察都来自 QEMU，**未在真机验证**
- ⚠️ 「绕过 magic 检查」在真机上是否有副作用（例如后续依赖 count>0 的分支）未知

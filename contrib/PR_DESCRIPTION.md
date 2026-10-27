# ZV-1F 装载器链路打通 + backup 基础设施 + fontlist 修复

> 来自 ZV-1F 新菜单移植项目的独立验证团队。
> 我们用自建 QEMU 环境独立走通了装载器全链路，有几个成果对本项目直接有用。

---

## TL;DR

1. **装载器三道 magic 全部解码并放行** → DMP_2D_Initialize : 0，App 能走到 appFw/stella
2. **backup_db 基础设施**（生成器/校验器）—— 合成 DB 可让 258 个 id 全部命中
3. **fontlist fallback 修法**（已实测 abort 消失）
4. **6 个二进制补丁**（libObj / backup.ko / stream.ko）—— 均带指纹校验

---

## 一、装载器三道 magic（`patches/patch_libobj_*.py`）

libObj 装载器 `0x206d04` 的完整控制流：

```
get_datasize(0x206d2c) → DISCRIM 钉死 28
malloc(28) → Backup_read(id, buf)
① BIN!  @buf[0]   0x206d54  失败码 r4=-1   → patch_libobj_discrim.py 放行
② size  @buf[4]   0x206d5a  仅警告（不拦截）
③ count_A@buf[8]  0x206d72  → patch_libobj_bypass_cnf.py 归零
④ count_B@buf[12] 0x206d78  → 同上
⑤ CNF!  @buf[16]  0x206d82  失败码 r4=-8   → 同上放行
⑥ PTN!  @buf[20]  0x206e9c  失败码 r4=-9   → 同上放行（★循环后！容易漏）
三道全过 → new[](count_A*16) + new[](count_B*52) + sentinel(8)
count=0 = 空列表，完全合法
```

**DISCRIM 的 NOP bug**（我们踩过）：`bf 00 bf 00` 是两条 `lsls r7,r7,#2` 不是 NOP！
正确写法是 `00 bf 00 bf`（逐半字反转）。已加文件字节序指纹校验。

## 二、backup_db 基础设施（`tools/`）

- `make_backup_db.py`：内核 RAM 布局生成器（页表 + 槽表 + 数据），自检 258/258
- `make_backup_db_v3.py`：零值 DB 生成器（过滤池残影页）
- `check_db.py`：DB 结构校验器（对照 Data_SearchId 算法）

**Data_SearchId 算法**（从 backup.ko 0x31fc 反汇编）：
```
page = id >> 16;  sub = id & 0xffff
页表[page] → {count, cum_base}（6B/页）
槽表[cum_base + sub] → v = (type<<24)|off
type = size（0=间接，0xFF=查表）
v == 0xFFFFFFFF → 空槽（返回 -1，正确行为）
```

## 三、fontlist fallback 修法（`tools/fontlist_fallback_test.sh`）

TextRM 初始化读 fontlist 的失败链：
```
Backup_get_datasize(0x810000) → 28 ✓
Backup_read(0x810000, buf) → 未确认发出（op3 不见）
→ 走 fallback fopen("TEXTRM_FONT_LIST", "r")  ← 字面量，不是 getenv！
→ 文件不存在 → 返回 -1 → 断言 → abort
```

修法：在候选 cwd 创建 `TEXTRM_FONT_LIST` 文件（内容 = fontlist.dat）。
实测：**abort 消失**，InfraIndctr 进程过了 TextRM。

## 四、补丁清单（`patches/`）

| 补丁 | 做什么 | 开关 |
|---|---|---|
| `patch_libobj_discrim.py` | get_datasize 钉死 28 + BIN! 放行 | DISCRIM |
| `patch_libobj_bypass_cnf.py` | CNF!+PTN! 放行 + count 归零 | BYPASSCNF |
| `patch_libobj_dtor_zero.py` | 析构垃圾字段置零（Conf 类 +0x0C/+0x10） | — |
| `patch_libobj_dtor_zero2.py` | 兄弟类 0x205d84 析构置零 | — |
| `patch_stream_pfn.py` | VM_PFNMAP 根治（stream_mmap bic→nop） | STREAMPFN |
| `patch_backuposal.py` | qid 0x3e014d 处理器强制注册 | BACKUPOSAL |

全部带**文件字节序指纹校验**（不从 objdump 列转抄）。

## 五、与本项目已有工作的对照

| 你们的 | 我们的 | 状态 |
|---|---|---|
| DMM 协议 RE | 未做 | 你们领先 |
| grm_ma 分派表 | 未做 | 你们领先 |
| 真机取证 | 无相机 | 你们领先 |
| 装载器三道 magic | 全过 + DMP_2D : 0 | **我们领先** |
| backup_db 基础设施 | 258 条自检通过 | **我们领先** |
| fontlist fallback | abort 消失（实测） | **我们领先** |
| VM_PFNMAP 根治 | Bad page map 60→0 | **我们领先** |

## 六、已知待解

1. Backup_read(0x810000) 的 op3 事务未在内核侧确认（get_datasize 有 op0 但 read 无 op3）
2. AppFw 进程 raise(SIGABRT) 内部 illegal instruction（Thumb 蹦床同族？）
3. stella HeapAllocator(nil) 池基址为 nil
4. MonDrv FixedArea 读走非 uipc 路径（可能在另一个进程）

## 七、怎么用

```bash
# 补丁（在 usr 镜像构建时用）
python3 contrib/patches/patch_libobj_discrim.py  libObj.so
python3 contrib/patches/patch_libobj_bypass_cnf.py libObj.so
python3 contrib/patches/patch_libobj_dtor_zero.py  libObj.so
python3 contrib/patches/patch_libobj_dtor_zero2.py libObj.so
python3 contrib/patches/patch_stream_pfn.py        kmod/
python3 contrib/patches/patch_backuposal.py        kmod/backup.ko

# backup_db 生成 + 校验
python3 contrib/tools/make_backup_db.py backup_db.bin
python3 contrib/tools/check_db.py backup_db.bin

# fontlist fallback 验证
bash contrib/tools/fontlist_fallback_test.sh
```

# 三条路线对比：混合固件 / A7C2 Gen5 迁移 / 修好现有注入

2026-11-10。给"哪个好整"一个基于实测数字的答复。

## 结论先说

**修好现有注入（路线 C）明显最好整，其次是混合固件（路线 A），A7C2 Gen5 迁移（路线 B）不可行。**

排序依据是"需要补上的未知物数量"，不是工作量感觉。

---

## 路线 A：混合固件（ZV-1F / ZV-E10M2 的新 UI 搬进 ZV-E10）

`/mnt/build/fw/port_m2/out/summary.md` 已经做过完整装配，结论是**装得下**：

| 项 | 值 |
| --- | --- |
| 合并后 `/usr` 内容 | 238,652,994 B (227.60 MiB) |
| nflasha15 上限 | 314,572,800 B (300.00 MiB) |
| ext2 镜像估算 | 258.32 MiB，余量 41.68 MiB |

**优点**：目标模型（ZV-1F）的库是完整的、自洽的，不需要凭空造符号。
**代价**：`TAKE_F` 136 个文件 / 37 MB，`ADD_F` 193 个 / 148 MB，`DROP_OLD_UI`
238 个文件 / 97 MB。实质是把整个 UI 层换掉。

**一个关键的乐观数字**：ZV-E10 的 194 个 ELF 里，只有 **1 个**（`libObj.so` 自己）
在 `DT_NEEDED` 里直接点名 `libObj.so`。也就是说 `libObj.so` 的替换面很窄，
而它恰恰是混合方案里必须换掉的那个（21,305,528 → 3,931,624）。

**难点**：`DROP_OLD_UI` 会删掉 `viewUnified2.so` 与 `CautionConfig.so`，也就是
现在这套 hook 的全部落点。混合方案一旦成型，之前为它们写的库就整套作废。

---

## 路线 B：直接让 A7C2 的新菜单跑起来（Gen5 / ASTRA）

用户提供的那份日志说明：**那套东西由 5 个未开源的库组成**。

```
/log/g5/libOb5.so           71.1 ms
/log/g5/libmp5.so           97.2 ms
/log/g5/libObjShim.so        1.5 ms
/log/g5/AdjChkConfig.so      5.5 ms
/log/g5/libappFwBoot_g5.so   1.3 ms
```

本地实测：`libOb5.so`、`libmp5.so`、`libObjShim.so`、`libappFwBoot_g5.so`
**四个都不存在**；只有 `AdjChkConfig.so` 有（`port_m2/out` 与 ZV1M2 树里）。

所以那份日志演示的是**别人手上的环境**，不是我们有的。

**依赖也够不上**。ZV-E10 的原厂库能提供 A7C2 `appFw.so` 所需 753 个符号里的 576 个，
**缺 177 个**，包括 `GRM_bitmapCreateFromPhys`、`_ZN2ux6gfxeng10TextSprite*`、
`_ZN10InfraInput12SetKeyRepeatEibii` 这类图形与输入层的核心入口。
这些属于 Gen5 新框架，ZV-E10 的 Gen4 库里根本没有对应实现。

**结论**：路线 B 需要那 4 个不开源的库，否则不可能。不是"难"，是缺前提。

---

## 路线 C：修好现有注入（推荐）

现有这套 `libnewmenu.so` 的思路是**在原厂菜单树上多挂一个节点**，不更换任何原厂库。
它的全部落点只有 4 个函数地址，全部已逐字节对拍过真机固件：

```
CautionConfig.so + 0xb8dffa   MATCH
viewUnified2.so  + 0x3014f0   MATCH
viewUnified2.so  + 0x2fccc0   MATCH
viewUnified2.so  + 0x862130   MATCH
```

QEMU 里这套逻辑已经跑通（`native-window-v14`，22 张截图），且用的就是 ZVE10 自己的
`viewUnified2.so` / `libObj.so` / `CautionConfig.so`（sha256 已核对一致）。所以缺的
不是"菜单能不能出来"，而是**真机上库有没有被加载**。

而库没加载的原因现在已经查清：

```
/etc/ld.so.preload 明明写了两个库
晚起进程（startBTCore.sh）有 4 个 libnewmenu 映射
im.elf 却 0 个
```

`env.txt` 那条路也查清了：不是 mmap 失败，是 `/setting/env.txt` **从来没被写进分区**
（10-25 与 10-27 两份真机镜像里都没有该文件；`dmode` 在 10-27 镜像里存在，所以门禁是开的，
是文件本身缺失）。

**⇒ 路线 C 缺的是一次数得清的验证，不是一堆未知物。**

---

## 为什么路线 A 不该现在做

1. **风险不可逆**：`/usr` 从 226 MB 换成 238 MB，要重写 nflasha15。回滚需要 300 MB 的
   完整备份，而现有备份 `nflasha15.img` 是 314,572,800 B，写回去要几十分钟且全程不能断电。
2. **收益被路线 C 吸收**：路线 A 的目标是"出现新菜单"。路线 C 已经能在 QEMU 里做出
   那一行，且不改任何原厂库。
3. **作废已有的工作**：`DROP_OLD_UI` 删掉的正是这套 hook 的落点。
4. **验收标准没变**：用户要的是原厂菜单里多一行，不是换成另一个机型的外观。

---

## 建议的下一步（路线 C）

在一次干净冷启动后，跑一条只读命令把加载失败的原因定位掉：

```sh
cat /proc/sys/kernel/random/boot_id         # 是不是新 boot
cat /proc/157/maps | grep -c libnewmenu     # 0 还是非 0
cat /proc/157/environ                       # LD_PRELOAD 在不在
cat /etc/ld.so.preload                      # 加载器被告知了什么
ls -la /usr/lib/libnewmenu_usr.so           # 该路径这次启动存在吗
cat /log/newmenu.log                        # 库自己的报告
```

判别表：

| 现象 | 原因 | 修法 |
| --- | --- | --- |
| `LD_PRELOAD` 在、无映射、无库日志 | 库解析失败 | 补齐 `libdl/libpthread/librt`，或用 `ldd` 核 |
| 路径文件不存在 | 部署没落到 `/usr` | 重写 `/usr` 上的库 |
| `boot_id` 未变、environ 无 `LD_PRELOAD` | 快照恢复，没重跑 init | 拔电池真冷启动 |
| preload 内容与映射不符 | 文件在 im.elf 之后被改 | 确认 agent 写入顺序 |

如果只读检查显示 preload 这条链对 `im.elf` 就是不可靠，再退到 `env.txt` 路线，
那时需要两个文件：`/setting/mode/dmode`（10-27 镜像里已有）与 `/setting/env.txt`
（历史上从没写过，这次要真的写进去）。

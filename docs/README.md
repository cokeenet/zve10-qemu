# docs 索引（2026-10-28 整理）

`/root/fw/oss/docs` 下原有 247 个平铺文档，已按主题归入子目录。
**根目录只留"每次开工都要看的"**，其余按主题进去找。

## 根目录（必读）

| 文件 | 作用 |
|---|---|
| `NEWMENU_STATUS.md` | ★ **新菜单移植的当前状态单页** —— 与 `QEMU_M2_PROGRESS.md` 配套，先读这个 |
| `TOOLKIT_REFERENCE.md` | 外部参考项目 `dino2048/zv-e10-toolkit`（MIT）：引进了哪些工具、怎么分工、注意事项 |
| `ROOT_TERMINAL_AND_SERVICE_MODE.md` | ★ Sony 服务模式（Senser）与 **root 终端**：相机能启动时一句话修 preload 的另一条路 |
| `STATUS_20261023.md` | 收工存档 + 开工操作单 |
| `FASTBOOT_OPERATIONS.md` | ★ **Pico 注入 / 换线 / stage2 / 读校验刷写的完整操作手册**（已对源码核对，含工具可用性表） |
| `SAFETY_RULES.md` | **规矩 0**：不得覆盖/写入新 `.so`，除非明确同意 |
| `ANTIBRICK_SYSTEM.md` / `BRICKPROOF_DESIGN.md` | 防变砖设计 |
| `MASTER_HANDOVER_NEWMENU.md` | ★ 移植 M2 新菜单的总交接 |
| `FASTBOOT_EMMC.md` | fastboot / eMMC 刷写原理 |
| `FIRMWARE_REPORT.md` | 官方升级包完整解密解包流程 |
| `GEN1_SYSTEM_STRUCTURE.md` / `GEN1_BOOT_HARDWARE_TYPEC.md` | 系统结构测绘 / 启动硬件 |
| `JIRITSU_CHANNEL.md` | JIRITSU 命令通道 |
| `EXTERNAL_ZVE10_RE.md` | 外部逆向成果对照 |
| `HANDOFF.md` / `HANDOFF_2026.md` | 历史交接包 |
| `ACKNOWLEDGEMENTS.md` | 参考项目与致谢 |

## 子目录

| 目录 | 内容 | 份数 |
|---|---|---|
| `rounds/` | **按轮次的推进记录** `ROUND4`→`ROUND29`、`SESSION_*`、`TOMORROW_*`、`REALCAM_GROUND_TRUTH_*`、`STATUS_*` | 27 |
| `incidents/` | **事故与恢复**：`INCIDENT_*`、变砖复盘、挂载失败 | 9 |
| `analysis/gpu/` | GPU / DMP / GRM / 软件 GPE / 光栅 | 23 |
| `analysis/menu/` | UXC / 菜单架构 / ZVEGISK / LVGL / 触摸 | 25 |
| `analysis/imdb/` | IMDB 加载器、dlopen、内核模块链 | 12 |
| `analysis/port/` | M2↔E10 移植、分区表、型号对比 | 26 |
| `analysis/kernel/` | 内核 / eMMC / 启动链 / Darwin | 11 |
| `analysis/system/` | 服务、IPC、ABI、崩溃分析（最大一堆） | 60 |
| `analysis/deploy/` | 部署方案、刷写评估、路线判断 | 21 |
| `analysis/qemu/` | QEMU 仿真路线 | 6 |
| `analysis/memory/` | 128 MB 内存墙 | 5 |
| `handoff/` | 其余 `HANDOFF_*` | 3 |

## 数据目录

| 目录 | 内容 |
|---|---|
| `baseline_factory_20261028/` | **原厂运行时基线**（im.elf PID/argv/maps/fd、mounts、cmdline、dmesg） |
| `artifacts/` | 采集产物（`baseline_stock/` 等） |
| `images/` | 截图（如 `zve10-menu-640x480.png`） |
| `hwpersist_input/` | 硬件持久化输入包 |

## 找东西

```bash
cd /root/fw/oss/docs
grep -rl "关键词" .            # 全文找
ls rounds/ | tail -20          # 看最近推进到哪
```

> 移动文件后如有引用断链，优先改引用，不要把文档搬回根目录。

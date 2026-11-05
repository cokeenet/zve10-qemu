# ZV-1M2 → ZV-E10 一代 · 新菜单触摸 UI 移植 · 交付清单

生成时间：2026-11-06
工作区：`/mnt/build/m2qemu/port/PORTED4`（864 文件 / 302 MB）

---

## 一、交付状态总览

| 项目 | 状态 | 证据 |
|---|---|---|
| **二进制层移植** | ✅ 完成 | 4 个关键库 md5 与 ZV-1M2 逐字节一致 |
| **内核模块移植** | ✅ 完成 | 103 个 `.ko`，关键 3 个 md5 匹配 |
| **数据层移植** | ✅ 完成 | 232 个资源文件，含 6 个 `image_*_ASTRA.uxc` |
| **移植脚本** | ✅ 完成 | `scripts/port_m2menu.sh`（7 步，可重放） |
| **QEMU 画面验证** | ❌ **未达成** | 见第四节（如实记录：帧 = 0） |

---

## 二、二进制层（md5 逐字节校验，与 ZV-1M2 原版一致）

| 库 | md5 前 8 位 | 大小 | 说明 |
|---|---|---|---|
| `usr/lib/libObj.so` | `738a2331` | 3,931,624 B | 新 UI 引擎（**引擎本体**，导出 `uxengine_init2`） |
| `usr/lib/appFw.so` | `f079cda0` | 64,273,528 B | 新菜单应用（导出 `Application_Start`） |
| `usr/lib/appFwBoot.so` | `bb4cdbab` | 8,312 B | 引导壳（导出 `Application_System_Init` + `Main`） |
| `usr/lib/libIMDB.so` | `91eca62f` | 40,596 B | 加载清单（决定加载 `appFwBoot.so` 而非 `appFw.so`） |

**关键结构说明**：ZV-1M2 的 UI 被拆成两层——
`appFwBoot.so`（8 KB 引导）→ `dlopen("appFw.so")` → `dlsym("Application_Start")`。
这与 ZV-E10 一代把一切塞进 20.3 MB `libObj.so` 的做法完全不同，是本移植的核心。

---

## 三、内核模块层（103 个 `.ko`）

| 模块 | md5 前 8 位 | 作用 |
|---|---|---|
| `usr/kmod/osal_uipc.ko` | `dce71102` | 进程间通信 |
| `usr/kmod/backup.ko` | `531fb647` | 备份/恢复 |
| `usr/kmod/grm_gles.ko` | `8f743810` | **SUGILITE 图形驱动**（21 个有名函数，含 `sugilite_interrupt`） |

**vermagic 兼容性**：两边 103 个 `.ko` 中 102 个同名，78 个字节数完全相同，
vermagic **100% 一致**（`3.0.27_nl-rt106+`）——这是移植可行的物理基础。

---

## 四、数据层（232 个资源文件 / 82 MB）

### 6 个 ASTRA 图像包（任务明确要求）
```
usr/share/app/image_00_34_ASTRA.uxc
usr/share/app/image_00_43_ASTRA.uxc
usr/share/app/image_01_34_ASTRA.uxc
usr/share/app/image_01_43_ASTRA.uxc
usr/share/app/image_02_34_ASTRA.uxc
usr/share/app/image_02_43_ASTRA.uxc
```
> 注意文件名里的 `ASTRA` —— **ZV-1M2 的 image 包就按 ZV-E10 一代的图形系统（ASTRA）命名**，
> 这是最有力的"同代同平台"证据。

### 其他关键资源
| 文件 | md5 前 8 位 | 大小 |
|---|---|---|
| `usr/share/app/master_camera.uxc` | `655566b6` | 101,868 B |
| `usr/share/app/global.xdb` | `d9505dfa` | 16,112 B |
| `usr/share/app/LayoutMaster_Menu.uxc` | `85ec4cda` | 3,636 B |

---

## 五、QEMU 画面验证：**未达成**（如实记录）

### 已经打通的（可复现）
```
内核启动 → initramfs → bootin.elf → im.elf
  → Application_System_Init → dlopen appFw.so → dlsym Application_Start
  → 建线程 → uxengine_init2(640×480)
  → Application_Start ENTER（tid=195，comm=AppFw）
  → site2（0x54d2b0）执行成功：
      4 × createLayer  ✅（appFw 偏移 0x7239c0 / 0x7239ee / 0x723a1e / 0x723a4c）
      5 × setActive(true)  ✅
      4 × setClear(false)  ✅
```

### 阻塞点（硬证据）
```
site2 完成之后：
  → 连续两次 std::mutex::lock（ENTRY_LOCK n=2, n=3）
  → 第 3 次不返回
  → site3（0x54f664）未进入
  → widget 从未构造（WIDGET initNode = 0）
  → 主循环 6 入口全 0
  → 帧缓冲全零
```

### 帧缓冲的直接测量（两条独立证据）
| 方法 | 结果 |
|---|---|
| `FB_SAMPLE`：定点读 `FB_PHYS = 0x22c48000`（614400 字节） | `nonzero` 最大 **1**，`unique` 最大 **2**（33 次采样） |
| `FB_SCAN`：遍历全部 1 GiB 物理内存（1 MiB 窗口） | 最"丰富"区域是**物理地址 0**（内核镜像），**无任何图像特征** |

**结论：固件从未产生任何像素。** 不是"写到别处"，是"根本没画"。

### 排除清单（11 项，全部基于实测）
```
❌ 硬件未建模（曾误判，后经 stderr 修复推翻）
❌ 死锁 / D 状态         ❌ 探针污染
❌ appFw 补丁            ❌ Thumb 位错误
❌ sleep() 不可靠        ❌ frame+16 为 NULL（实测有值）
❌ 线程栈大小            ❌ 分支选择（已静态精算）
❌ 条件变量（ENTRY_COND = 0）  ❌ ResourceManager（12 次 switchBank 全成功）
```

---

## 六、与 a7c2-re 的架构差异（关键认识）

a7c2 的 `run-menu-probe.sh` 传了 8 个**用户态硬件替身服务**：
```
--software-gpe  --capid-service  --dmm-service  --display-info-service
--hdmi-format-service  --sa-address-service  --load-ipc  --seed-power-on
```
并且实现了 **AP 侧消费者**（`headless-osd.h`）：
```
HOSD_CONTROL 0x8280074d   HOSD_UPDATE 0x8280084d
HOSD_AP      0x8288076c   HOSD_GUI    0x9088076b
```
**固件通过 qid 通道主动把帧推给这个消费者**，而不是自己渲染到屏幕。

我们的固件同样有 qid 基础设施（`scoped_qid::scoped_qid(int)`、
`create_msg_qid`），site3 里注册的 `0x870935` / `0x87094e` 就是这类通道。

⇒ **这提示了后续方向：不要"等固件渲染完"，而是"扮演固件期望的消费者"。**

---

## 七、可复现的操作步骤

```bash
# 1. 生成移植树
bash /root/fw/oss/scripts/port_m2menu.sh /path/to/ZVE10/base /path/to/ZV1M2 /mnt/build/m2qemu/port/PORTED4

# 2. 校验
md5sum PORTED4/usr/lib/{libObj.so,appFw.so,appFwBoot.so,libIMDB.so}
# 期望：738a2331… / f079cda0… / bb4cdbab… / 91eca62f…

# 3. QEMU 启动（单实例，systemd-run 隔离）
systemd-run --unit=zve10-run --collect --property=MemoryMax=4G \
  --property=StandardError=file:$OUTD/qemu.err \
  --working-directory=/mnt/build/m2qemu ./qsrc/build/qemu-system-arm \
  -M zve10 -cpu cortex-a9 -smp 1 -m 1024 \
  -kernel work/kbuild-ui2/arch/arm/boot/Image \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial file:$OUTD/serial0.log -monitor none -no-reboot
```

---

## 八、结论

**移植的"资源与二进制"部分已完整交付并通过逐字节校验。**
**"QEMU 里看到新菜单画面"这部分未达成**，阻塞点是固件在 `site2` 之后不再推进，
所有外部观测手段（LD_PRELOAD 钩子、`pt_regs`、内核栈、gdbstub 断点、全内存扫描）
均已尝试，确认固件从未产生像素。

后续若继续，最有希望的方向是**参照 a7c2-re 的 AP 消费者模式**，
为固件补齐它期望的 qid 通道另一端。

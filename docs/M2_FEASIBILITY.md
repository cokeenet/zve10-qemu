# ZV-1M2 (M2) 新菜单 · QEMU 可行性总报告

**日期**：2026-11-05 · **轮次**：1–30 · **结论**：**未出帧**（帧数 = 0，可重复 0 次）

本文件是对 30 轮工作的诚实盘点：**什么已经通了、卡在哪、下一步具体该做什么**。
所有结论都有日志/反汇编证据，撤回的结论单独列出。

---

## 一、已验证打通的部分（有硬证据）

| # | 事实 | 证据 |
|---|---|---|
| 1 | `im.elf` 进程**活着**，不崩溃、无死锁 | `APPSTART still-inside t=105s`，21 次心跳 |
| 2 | 64 个线程全部在正常等待（消息队列/futex/timer） | `TS pid=114 nrunning=1 \| osal_rcv_msg_tmo:24 futex:30` |
| 3 | `Application_System_Init`（appFwBoot 284B）走 **AppFw 分支** | `dlopen appFw.so → 0xb92c8`，`hooked Application_Start` |
| 4 | `Application_Start` **被进入** | `ARGTRACE >>> Application_Start ENTER` |
| 5 | `uxengine_init2(640,480,0x80000000,0,0)` **执行成功** | 跳过它 ⇒ `Application_Start` 进不去（ENTR=0），反证其成功 |
| 6 | **GraphicsEngine 单例存在** | `libObj_base + 0x357490` 读出非零指针 |
| 7 | **`drawFramebuffer` 能执行且真的干活** | 强制调用后对象 3 个字段变化（`o[8]: 0→0x044bf53d` 等） |
| 8 | `drawFramebuffer` 走的是 640×480 渲染路径 | 内部 `blx getFramebufferWidth/Height@plt` |
| 9 | GPE 硬件端口已映射 | `! ! ! GPE HW: bar remapped 0xf1000000 size 4000` |
| 10 | 154 个库被查找，118 个成功 init | `LD_DEBUG=libs` 权威数据 |

## 二、卡住的地方

| # | 阻塞 | 证据 |
|---|---|---|
| 1 | **53 个库加载失败**（含 `libgpe.so` / `gui.so`） | `LD_DEBUG`：`trying file=/usr/lib/libgpe.so` 后无 `calling init` |
| 2 | 这 53 个库在 **ZV-1M2 全固件（所有分区）都不存在** | 遍历 `COMPARE/extracted/ZV1M2` + 4 个分区镜像，命中 1/53 |
| 3 | 跨固件借库**不可行**：glibc 2.11 vs 2.17 | ZVE10M2 的 `libgpe.so` 需 `memcpy@GLIBC_2.17` |
| 4 | **无人调用 `drawFramebuffer`** | 扫 appFw/libObj 全部 BL 指令 → 0 处（阳性对照：`osal_rcv_msg_tmo@plt` 4 处） |
| 5 | 菜单 controller **从未被创建** | 无 screen/sequence 创建痕迹；a7c2 第 3 步未完成 |

## 三、30 轮里最重要的 5 条经验（都已写入账本）

1. **ARM 上“能读写 ≠ 能执行”**：同地址 `*(volatile*)` 成功、`bx` 过去 `prefetch abort`。
2. **`FSR=0x001` 是 Alignment fault，不是 Translation fault**：
   `drawFramebuffer` 符号值是 **奇数 `0xe565b`**（Thumb 函数），
   我用偶数 `0xe565a` 调用 ⇒ CPU 进 ARM 态 ⇒ 2 字节对齐 ⇒ 对齐错误。
   **这是 3 轮弯路的总根因。**
3. **`dladdr` 的 `dli_fbase` 在老 glibc 上不可信**：
   实测 `0x09f2a000` vs 真实 `0x0a0cd000`，差 1.6 MB。必须用 `/proc/self/maps` 的 `r-xp` 起点。
4. **匿名 `.bss` 会被按文件名过滤的 maps 探测漏掉**：
   害我一度误判 `self` 指针越界（实际落在 `.bss` 内，完全有效）。
5. **共享计数器做日志限流会造假**：
   两个函数共用 `g_ssd`，一个自增一个不自增 ⇒ 打印量翻 31 倍（6738 vs 216 行），
   我据此推理了 6 轮“重试了 655 次”全是假的。

## 四、下一步（按优先级，具体到可执行）

### A. 最高优先：确认 `libgpe.so` 在本机上的真身
53 个缺失库在**三个固件包里都基本没有**（ZV1M2 1/53、ZVE10M2 16/53、ZVE10 4/53）。
这意味着它们是 **IMDB 清单里的条件项**，由 profile 位 / 机型 / 区域筛选，
**不是“我们拷漏了”**。
→ 需要在一台**真机**上 `ls /usr/lib | grep -E 'gpe|gui'` 确认；
   若真机也没有，则 H-A 成立：**M2 的 UI 完全走 GPU，软件替身路线不成立**。

### B. 次优先：找到“创建菜单 controller”的入口
`drawFramebuffer` 是完整实现且可执行（已验证），但**没人调用它**。
a7c2 的第 3 步是“用原框架建立菜单”，我们完全没做。
→ 从 `ux::wgtsys::WidgetSystem` 的构造/`notifyDraw` 触发链入手；
   `notifyDraw`（78 字节）是比 `drawFramebuffer` 更早的一环。

### C. 若要继续 QEMU 侧：把 GPE MMIO 变成可观测
`GPE HW: bar remapped 0xf1000000 size 4000` 说明有 4KB MMIO 窗口。
→ 在自建 `zve10.c` 里给这段加 trace，记录所有读写，
   就能知道引擎到底有没有发绘图命令（比挂钩子更底层、更可靠）。

## 五、已撤回的结论（不得再引用）

- ❌ “日志队列被人卡住导致 `Application_Start` 阻塞” —— `0x8f0313` 根本不经过 `snd_sync_msg`
- ❌ “backup 队列重试 5619 次 / ulog 302 次 / `0x8f0313` 696 次” —— 计数器 bug 导致，真实是 103/106/0
- ❌ “GOT 偏移 `0x357490` 有 0x44 误差” —— 匿名 `.bss` 误判，偏移正确
- ❌ “`GRM_gpermFillrect` 是绘图入口” —— 全树 0 调用者
- ❌ “`libgpe.so` 是我们拷漏的” —— 固件里本来就没有
- ❌ “prefetch abort 是页表未映射 / QEMU MMU 缺陷” —— 实为 Thumb/ARM 模式错

## 六、可复现的验证命令

```bash
# 基线启动（约 7800 行日志，Application_Start ENTER=1）
cd /mnt/build/m2qemu && \
  systemd-run --unit=zve10-x --collect --property=MemoryMax=4G \
  --working-directory=/mnt/build/m2qemu \
  ./qsrc/build/qemu-system-arm -M zve10 -cpu cortex-a9 -smp 1 -m 1024 \
  -kernel work/kbuild-ui2/arch/arm/boot/Image \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial file:/tmp/s0.log -serial file:/tmp/s1.log -monitor none -no-reboot
# 确切 PID
systemctl show -p MainPID --value zve10-x
```


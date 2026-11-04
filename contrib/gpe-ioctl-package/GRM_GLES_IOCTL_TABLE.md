# /dev/dmpgles2（2D/GLES 设备）ioctl 命令面分析

> 分析对象：`lib/libObj.so`（用户态，vaddr == 文件偏移）+ `kmod/grm_gles.ko`（内核驱动，**未 strip，符号全在**）
> 工具：`arm-linux-gnueabi-objdump -dr`（ARM/Thumb 混编，未用 capstone 线性扫）
> 只读分析，未运行 QEMU，未改动 `unpacked/`。

---

## 1. 结论

1. **`/dev/dmpgles2` 全固件只被 `libObj.so` 一个地方打开**：`rm_reg_init` @ `0x12c3c0`（Thumb，dynsym 名 `rm_reg_init`，size 0x2f0）。对应驱动是 `kmod/grm_gles.ko`，模块名 `grm_gles` v0.6、`description=SUGILITE Driver`、设备节点名 `"dmpgles2"`、字符设备 **248:29**。别的 `.so` 一个都没碰它（`grep -rl dmpgles2` 只命中 `libObj.so` 和 `grm_gles.ko`）。

2. **2D 命令根本不是用 ioctl 下发的**。`rm_reg_init` 先问驱动要"寄存器窗口物理基址 + 大小"（`0xc0048202` / `0x80048206`），拿到 **phys=0xF1000000, size=0x4000(16KB)**，再用 `/dev/mem` mmap 下来，**用户态直接 MMIO 读写这个 16KB 窗口**（diag 日志里能看到 `04090000-04094000 rw-s f1000000 /dev/mem` 已映射成功）。ioctl 只负责：窗口信息、suspend/resume、等中断、等/发事件、三把 HW 互斥锁。

3. **驱动里只有 3 个命令会阻塞**（`sugilite_ioctl` @ `0x8e4` 全分支已逐条读完）：
   - `0xC0048203` —— 等"中断状态位"（`prepare_to_wait` + `schedule_timeout`，最多 10 轮，**有超时上限**）；由 `sugilite_interrupt` 里的 `wake_up(&dev+0x4c)` 唤醒。
   - `0xC004820D` —— 等"本任务事件完成"（`wait_for_completion_interruptible_timeout(..., 10^7 jiffies)`，HZ=100 时 ≈ **27 小时**，等价永久）。
   - `0xC0048211` —— `down_interruptible(&dev[idx].sem)`（"mlock gain"，最多 3 把，idx 0..2），无超时。
   其余 10 条 ioctl 都是即时返回的取数/置标志/写单个寄存器。

4. **回答第 4 问（要拦哪个设备的哪个命令号）**：
   - 想"收到命令就立刻回一个完成事件" → 拦 **`/dev/dmpgles2` 的 ioctl `0xC0048203`**（真·硬件完成路径），让它立刻返回非 0。
   - 但它等的信号来自 **IRQ 76**（`request_threaded_irq(76, sugilite_interrupt, ...)`），而 IRQ 只会在 guest 写 `0xF1000000` 窗口之后才该来。**所以"根治"必须是 QEMU 在 `0xF1000000` 上加一个 16KB 的假设备：让 `+0x34` 读回带 `0x04000000` 位的值，并在用户态写完 kick 寄存器后拉 IRQ 76**；驱动 ISR 会自动 ack（`+0xc18`/`+0x1c`/`+0x3c`/`+0x44`）、累加 `dev->[0xa8]`、`wake_up`，`0xC0048203` 随即返回，上层继续。
   - ~~若实测卡的是 `0xC004820D`~~ **已排除**：它的唯一包装 `0x12d090` 只被 kick 线程 `0x12d282` 调用，
     而日志里卡住的是 `THREAD_INFRA0`（不是 libObj 造的线程），见 §6.4/§6.6。
     **现在的主嫌疑是 `0xC0048211`（mlock gain，纯用户态互斥锁，无超时，内核不参与）**，见 §6.5。

---

## 2. 设备打开点与调用链

### 2.1 谁打开 /dev/dmpgles2

`rm_reg_init` @ **0x12c3c0**（Thumb 函数，`push {r4-r9,sl,lr}` 之后 `sub sp,#8`）：

| 地址 | 指令/动作 | 说明 |
|---|---|---|
| `0x12c410`–`0x12c418` | `open("/dev/dmpgles2", 2)` | **打开点**（字符串 @ `0x2a1a30`，错误串 `"failed to open sugilite device"` @ `0x2a1a40`）；fd 存全局 |
| `0x12c424`–`0x12c42e` | `open("/dev/mem", 2)` | 为了 mmap 物理寄存器窗口 |
| `0x12c44e` | `ioctl(fd_gles, 0x80048206, &size)` | 取窗口大小 → 失败打印 `"can't get register size"` |
| `0x12c46c` | `ioctl(fd_gles, 0xc0048202, &idx=1)` | 取窗口物理基址 → 失败打印 `"can't get register's physical address"` |
| `0x12c56c` | `mmap(NULL, size, PROT_READ\|WRITE, MAP_SHARED, fd_mem, phys)` | **拿到 GPE 寄存器窗口**（phys=0xF1000000, size=0x4000），基址存全局 |
| `0x12c582` | `ioctl(fd_gles, 0x80048263, &flag)` | 读驱动全局标志 → 打印 `"!!! GPE HW: PSC dummywrite %s."`（enabled/disabled） |
| `0x12c6d6` | `ioctl(fd_gles, 0xc0048208, &r)` | suspend（clock down）→ 错误串 `"failed to get ioctl hw suspend value..."` |
| `0x12c736` | `ioctl(fd_gles, 0xc0048209, &r)` | resume（clock up）→ 错误串 `"...hw resume value..."` |
| `0x12c796` | `ioctl(fd_gles, 0x8004820a, &r)` | is-suspended → 错误串 `"...hw issuspended value..."` |

`rm_reg_init` 之后还配套 5 个薄封装（同一模块，紧邻在 0x12cc00–0x12d240）：

| 函数起点 | 内容 | 对应错误串 |
|---|---|---|
| `0x12c2b0` | `ioctl(fd, 0xc004820f, &val)` @0x12c2d0 | `"can't set hw bypass feature."` |
| `0x12cc00` | `ioctl(fd, 0xc0048203, &mask)` @0x12cc58 | — |
| `0x12d090` | 循环 `ioctl(fd, 0xc004820d, &st)` 直到 `st != 0` @0x12d0b4 | `"failed to wait signal from sugilite device"` |
| `0x12d0f0` | `ioctl(fd, 0xc004820e, &st)` @0x12d110 | `"failed to send signal with sugilite device"` |
| `0x12d150` | 循环 `ioctl(fd, 0xc0048211, &idx)` 直到回 0 @0x12d16e | `"mlock gain failed"` |
| `0x12d1c0` | `ioctl(fd, 0xc0048212, &idx)` @0x12d1e0 | `"mlock release failed"` |
| `0x12d220` | `mmap(NULL, 0x20000, RW, SHARED, fd_gles, 0)` @0x12d234 | `"mmap to shared mem failed"` |

### 2.2 与 DMP_2D_Initialize 的调用链（**已确证**）

```
grm_init                        @ 0x11683c   （dynsym 导出）
  ├─ fputs(stderr, "!!! GLES Vecgen enabled./disabled.")   ← app.err 里最后一行
  ├─ 0x116800(0x4000)
  ├─ 0x114dd0()                          // grm 侧小初始化
  ├─ r0 = DMP_2D_Initialize()            // bl 0x182120  @ 0x1168d2   ← 从不返回
  └─ fprintf(stderr, "===== DMP_2D_Initialize : %d", r0)  @ 0x1168d6/0x1168de
                                        // 字符串 @0x29c859，就挨着 grm 的 C++ RTTI/符号串
```

`DMP_2D_Initialize` @ `0x182120` **只是一个 3 条指令的 thunk**：

```
0x182120: ldr r0, [pc, #12]      ; 取 &g_2d_obj（运行时 ≈ 0x241861f0）
0x182124: add r0, pc
0x18212a: pop {r7}
0x18212c: b.w 0x181700           ; ← 真正的实现
```

`0x181700` 是"**初始化 once + 建 worker 线程 + 等初始化完成**"的模板：里面 `pthread_key_create`/`pthread_mutex_lock`/`pthread_create` 一堆，并在 `0x1817cc`、`0x181820`（还有 `0x181896`/`0x181902`/`0x18198c`/`0x181a20`/`0x181a94`…）等处 `pthread_cond_wait`。也就是说 **DMP_2D_Initialize 是在等另一个线程把 2D 子系统初始化完**；那个线程本身才是真正碰 `/dev/dmpgles2` + `0xF1000000` 的人。

`0x181700` 到 `0x12xxxx`（GRM/rm 层）的调用是**间接的**（虚表 / 回调 / 线程入口），静态 `bl` 图不连通——所以"具体哪个线程"只能靠运行时证据（见 §4.5）。

### 2.3 驱动侧（grm_gles.ko，未 strip，直接读符号）

```
sugilite_dev_init   @0x1568   dev 结构 0x10c 字节：
                              [0xec]=2, [0xf8]=0xF1000000, [0x100]=0xF1000000,
                              [0xfc]=0, [0x104]=0, [0x108]=0x4000
sugilite_chdev_register @0x0  request_threaded_irq(irq=76, sugilite_interrupt, NULL,
                                                  flags=0x84, name="grm_gles", dev)
sugilite_open       @0x434    建 sugilite_event{key=current->[0x108], done=0,
                                                  completion@+0x14, wq@+0x18}
sugilite_interrupt  @0x640    base=dev->[0xf8]=0xF1000000
                              st = ioread32(base+0x34)
                              if (st & 0x40000000) iowrite32(0, base+0xc18)
                              if (st & 0x04000000) iowrite32(0, base+0x1c)
                              if (st & 0x0000000f) { iowrite32(1,base+0x3c);
                                                     iowrite32(1,base+0x44); }
                              dev->[0xa8] |= (st & 0xffff0000) | ...
                              wake_up(&dev->[0x4c])     ← 唤醒 0xC0048203
sugilite_ioctl      @0x8e4    _IOC 解码：dir/size/type=0x82/nr
sugilite_mmap       @0x62c    /dev/dmpgles2 的 0x20000 共享区
```

**关键点：`sugilite_interrupt` 只 `wake_up`，不做 `complete_all`。** 也就是说 `0xC004820D` 的完成**全靠用户态另一个线程调 `0xC004820E`**（`complete_all(&event->completion)`），驱动 IRQ 不参与。这两条是两套机制，不要混：

- `0xC0048203` = **等硬件中断状态位**（真 HW 完成）
- `0xC004820D` / `0xC004820E` = **同进程线程间事件**（软件 condition variable，key = 任务标识）

---

## 3. ioctl 命令号表

全部满足 Linux `_IOC` 编码：`(dir<<30)|(size<<16)|(type<<8)|nr`，**type 恒为 0x82，size 恒为 4**（驱动统一用 `__get_user_4` / `__put_user_4`，所以**每个命令的入参都是一个 4 字节 int 的指针**；调用点的 `movs r2,#4` / `add r2,r7,#4` 也都指向 4 字节栈变量）。

| 命令号 | dir | size | nr | 用户态调用点 | 所在函数起点 | 驱动分支 | 语义（从驱动反推） | 阻塞 |
|---|---|---|---|---|---|---|---|---|
| `0x80048200` | R | 4 | 0x00 | —（libObj 未用） | — | 0xa0c | `put_user(dev->[0xec])` = 2（bank/索引上限） | 否 |
| `0xc0048201` | RW | 4 | 0x01 | —（未用） | — | 0xa34 | in=idx；out=`dev->[0x104+idx*4]`（idx1→0x4000） | 否 |
| `0xc0048202` | RW | 4 | 0x02 | `0x12c46c` | `0x12c3c0` | 0xa74 | in=idx（0→返回0x80000000）；out=`dev->[0xfc+idx*4]`，**idx1→0xF1000000** | 否 |
| `0xc0048203` | RW | 4 | 0x03 | `0x12cc58` | `0x12cc00` | 0xabc | in/out=中断状态掩码；`prepare_to_wait`+`schedule_timeout` 最多 10 轮等 `dev->[0xa8]&mask`；mask 必须含 `0xc4000000` 位 | **是（有上限）** |
| `0x40048204` | W | 4 | 0x04 | —（未用） | — | 0xd8c | 空操作，返回 0 | 否 |
| `0x80048205` | R | 4 | 0x05 | —（未用） | — | 0xd94 | `put_user(dev->[0x104])` = 0 | 否 |
| `0x80048206` | R | 4 | 0x06 | `0x12c44e` | `0x12c3c0` | 0xdb8 | `put_user(dev->[0x108])` = **0x4000 = 寄存器窗口大小** | 否 |
| `0x80048207` | R | 4 | 0x07 | —（未用） | — | 0xddc | `put_user(0x200)` 常量 | 否 |
| `0xc0048208` | RW | 4 | 0x08 | `0x12c6d6` | `0x12c6b0` | 0xe00 | **suspend**：加锁 → `dev->[0x48]=1` → `sugilite_clock_down_internal()`；out=1(首次)/0(已suspend) | 否 |
| `0xc0048209` | RW | 4 | 0x09 | `0x12c736` | `0x12c710` | 0xe60 | **resume**：`clock_up_internal()` → `dev->[0x48]=0`；out=1/0 | 否 |
| `0x8004820a` | R | 4 | 0x0a | `0x12c796` | `0x12c770` | 0xec8 | `put_user(dev->[0x48])` = is-suspended | 否 |
| `0xc004820c` | RW | 4 | 0x0c | —（未用） | — | 0xeec | `atomic_inc(dev->[0x44])` 后回值 | 否 |
| `0xc004820d` | RW | 4 | 0x0d | `0x12d0b4` | `0x12d090` | 0xf2c | **等本任务事件**：pending 标志则立即回 1，否则 `wait_for_completion_interruptible_timeout(&ev->comp, 10^7 jiffies)` | **是（≈永久）** |
| `0xc004820e` | RW | 4 | 0x0e | `0x12d110` | `0x12d0f0` | 0xfec | **发事件**：in=key(0则用当前任务)；`complete_all(&ev->comp)`；找不到时 out=`0xE0000000`（用户态判 `== -1`） | 否 |
| `0xc004820f` | RW | 4 | 0x0f | `0x12c2d0` | `0x12c2b0` | 0x121c | **写单个寄存器**：`iowrite32(val\|0x20000000, BAR+0xc0)`，并记 `dev->[0xf0]=val`（= "hw bypass feature"） | 否 |
| `0xc0048211` | RW | 4 | 0x11 | `0x12d16e` | `0x12d150` | 0x115c | in=idx(≤2)；`down_interruptible(&dev[idx].sem)` = **mlock gain**（HW 访问互斥） | **是（可无限）** |
| `0xc0048212` | RW | 4 | 0x12 | `0x12d1e0` | `0x12d1c0` | 0x11bc | in=idx(≤2)；`up()` = **mlock release** | 否 |
| `0x80048263` | R | 4 | 0x63 | `0x12c582` | `0x12c3c0` | 0x1268 | `put_user(*(u32*)0x320)` = 驱动全局标志（PSC dummywrite 开关） | 否 |

> 说明：`nr=0x05/0x07/0x63` 的 size 字段也编码成 4，实际语义是"读一个标量"。
> libObj 实际只用了 **12 条**：0x02/0x03/0x06/0x08/0x09/0x0a/0x0d/0x0e/0x0f/0x11/0x12/0x63。

---

## 4. 证据与复现命令

### 4.1 设备名字符串归属

```
$ cd unpacked/zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked
$ grep -rl dmpgles2 .
./kmod/grm_gles.ko
./lib/libObj.so
```

`grm_gles.ko` 的 `strings`：`dmpgles2` @0x2071、`<6>target wrong NULL`、`description=SUGILITE Driver`、`vermagic=3.0.27_nl-rt106+`。

### 4.2 驱动 ioctl 分支（`sugilite_ioctl` @0x8e4）

```bash
# 带重定位反汇编（关键：bl 0 <...> 那些要靠 R_ARM_CALL 才能认出来）
arm-linux-gnueabi-objdump -dr kmod/grm_gles.ko > ko_grm_gles_r.dis
sed -n '/<sugilite_ioctl>:/,/^$/p'  ko_grm_gles_r.dis
```
比较链本身就摊在 `0x910`–`0xa08`，全部常量集中在字面量池 `0x12b8`–`0x12d0`：

```
0x12b8: c0048202   0x12bc: 8004820a   0x12c0: 80048263   0x12c4: c0048201
0x12c8: c004820d   0x12cc: c0048209   0x12d0: c004820f
中间值由 add/sub 常量推出：0x80048206 / 0x80048200 / 0x80048205 / 0x40048204 /
0x80048207 / 0xc0048208 / 0xc0048203 / 0xc004820c / 0xc004820e / 0xc0048211 / 0xc0048212
```

`rm_reg_init` 侧的 12 个调用点用 `movw r1,#0x82xx` + `movt r1,#0x8004|0xc004` 拼命令号
（例：`0x12c440 movw r1,#0x8206` / `0x12c446 movt r1,#0x8004` → `0x80048206`）。
扫描脚本：`work/scan_ioctl2.py`（解析 `libObj.dis`，回溯 r1 的 movw/movt 组合）。

### 4.3 PIC 字符串 xref

`libObj.so` 的取串惯用法是 `ldr rX,[pc,#n] ; (POOL)` + 紧随 `add rX, pc`，
**target = *(u32*)POOL + add指令地址 + 4**（注意不是 `+4` 加在 POOL 上）。
脚本：`work/xref_multi.py`、`work/slice_dis.py`（按地址切片，比 grep 稳）。

关键命中：

| 字符串 | 地址 | 引用点 | 所在函数 |
|---|---|---|---|
| `/dev/dmpgles2` | 0x2a1a30 | ldr@0x12c410 | rm_reg_init |
| `failed to open sugilite device` | 0x2a1a40 | ldr@0x12c518 | rm_reg_init |
| `failed to wait signal from sugilite device` | 0x2a1c44 | ldr@0x12d0d4 | 0x12d090 |
| `failed to send signal with sugilite device` | 0x2a1c70 | ldr@0x12d134 | 0x12d0f0 |
| `===== DMP_2D_Initialize : %d` | 0x29c859 | ldr@0x1168d6 | **grm_init** |
| `!!! GLES Vecgen disabled.` | 0x29c83e | 同区 | grm_init |

### 4.4 DMP_2D_Initialize 是 thunk，且 grm_init 打印它的返回值

```bash
python work/slice_dis.py 0x182120 0x182140    # 3 条指令 + b.w 0x181700
python work/slice_dis.py 0x11683c 0x116930    # grm_init，0x1168d2 处 bl 0x182120
```

### 4.5 运行时证据（现成的 diag 日志，**不用再跑 QEMU**）

`work/diag14.txt`（T+3s…T+45s 的巡检快照）里：

```
131 comm=im.real
...
183 comm=grm_dn_vdf_if
 wchan=osal_rcv_msg_tmo          ← GRM 的显示线程在“等消息”，空转
184 comm=THREAD_INFRA0
 wchan=sugilite_ioctl            ← 有线程永久卡在驱动 ioctl 里（每个采样点都在）
```

同一个文件里的 maps：

```
04090000-04094000 rw-s f1000000  fc:07 138   /dev/mem        ← GPE 寄存器窗口 16KB，已映射成功
05f6f000-05f8f000 rw-s 00000000  fc:07 179   /dev/dmpgles2   ← 驱动 128KB 共享区，已映射成功
09be5000-0bbe5000 rw-s 20312000  fc:07 291   /dev/stream
```

`work/diag14.txt` 的 `app.err` 快照最后一行是 `!!! GLES Vecgen disabled.`，
**没有** `===== DMP_2D_Initialize`，与"0x182120 从不返回"完全吻合。
`work/diag.txt` 里也有设备节点：`crwxrwxrwx 1 0 0 248, 29 ... dmpgles2`。

### 4.6 怎么确定卡的是 03 / 0D / 11 中的哪一条（2 分钟，一次性）

`wchan=sugilite_ioctl` 分不出具体分支（三条都可能落在这个符号里）。最省事的判定：
用现成的 `work/ioctl_trace.s`（LD_PRELOAD 包 `ioctl`），把每条记录加"进入/返回时间戳"写到文件：

- 最后一条**只有进入、没有返回**的记录 → 就是它。
- `cmd=C004820D` → 卡在"等事件"（软件信号，等的是 `0xC004820E`）
- `cmd=C0048211` → 卡在 HW 互斥锁（有人拿了没放）
- 如果只是偶发 `C0048203` 超时 → 那是真 HW 完成路径，需要按 §1.4 做假设备

---

## 5. 未确定项

1. **`0x181700` → GRM/rm 层的具体间接跳转**：`rm_reg_init` / `rm_kick_thread_init` 都是导出符号但**没有任何静态 `bl` 调用者**，`sub_12D280`（唯一调用 `0xC004820D` 的那段）连函数指针字面量都搜不到（`0x12d281`/`0x12d091`/`0x12d0f1` 在文件里都不存在）。所以"到底哪个线程"只能靠 §4.6 的运行时证据，静态补不上。
2. **`0xC0048203` 的超时到底多长**：驱动用 `100 * jiffies_table[hz_idx] / 5` 再 `schedule_timeout`，循环 10 次；具体 tick 依赖 `dev->[0x74]` 对应的 HZ 表，没算死（只能确定"有上限，不是永久"）。
3. **`0xF1000000` 窗口里 2D 命令用哪些寄存器偏移**：只确证了驱动自己用的 `+0x34`（status）、`+0x1c`/`+0x3c`/`+0x44`/`+0xc18`（ack）、`+0xc0`（bypass）。**用户态写哪些偏移属于没确定**（`rm_reg_init` 只把 mmap 基址存进全局，后续读写分散在别处，且是间接寻址）。
   若要做假设备，最稳的起手式：**不解析语义，任何写都当作 kick → 立刻把 `+0x34` 的 bit30(`0x40000000`)+bit26(`0x04000000`) 拉起来并触发 IRQ 76**；ISR 会自己 ack 并 `wake_up`。
4. `0xc0048201` / `0x80048200` / `0x80048205` / `0x80048207` / `0xc004820c` 在 libObj 里**没有调用点**（可能是别的进程/别的版本用，或死代码），语义是从驱动分支反推的。
5. 驱动对 `0xc0048201` 的 `idx=2` 会读到 `dev->[0x10c]`，而 `sugilite_dev` 只分配了 0x10c 字节 —— **越界 4 字节**（潜在 bug，但不是我们的卡点）。
6. `pid%08x` / `%d_00_%08x_%x_%08x` / `"2_00_e80010c0_f_ffffffff"` 这几条串（0x2a1c24 起）和 `rm_rqueue_hw_log_init` 相关，看起来是 HW log 资源命名，未展开。

---

## 6. 交叉验证（与 qemu-model-auditor / condvar-analyst 对表）

### 6.1 完全一致的部分（两路独立推导，可信）

`BAR=0xF1000000` / `len=0x4000` / `IRQ=76` / `flags=0x84` / `BAR+0x34=status` /
`BAR+0xC18,0x1C,0x3C,0x44=ack` / `BAR+0xC0=doorbell` / `vmalloc(0x20000)` 共享区 / `type=0x82`。
两边都从 `grm_gles.ko` 反汇编独立读出，**数字完全对得上**。

### 6.2 需要修正的三处（qemu-model-auditor 版本）

1. **`0xc0048202` 不是"walk dev+0x4c waitqueue 的 blocking submit"**。
   字面量池原始字节（file off = vaddr + 0x34，`.text` vaddr==0）：
   `*0x12b8 = 0xc0048202`，对应 `0x910: cmp r1,r3 ; 0x918: beq 0xa74` → 分支 **0xa74** 是纯取数：
   `__get_user_4` 读 idx → `[dev+0xfc+idx*4]` → `__put_user_4`，**不阻塞**。
   用 `dev+0x4c`（`add sl,r4,#76` @0xb74/0xc10）的是 **`0xc0048203`** 的分支（0xabc）。
2. **信号量（`down_interruptible` / `up`，`dev+0xb0+idx*0x14`）是 `0xc0048211` / `0xc0048212`，不是 `0xc0048209`**。
   `0x9f0: add r3,r3,#2` → `0xc0048211` → `0x11 5c`(down)；`0x9fc: +1` → `0xc0048212` → `0x11bc`(up)。
   `0xc0048209`（分支 0xe60）是 **resume：`sugilite_clock_up_internal()` + `dev[0x48]=0`**。
3. **"ioctl 的等待都有超时，驱动不会永久挂住"这个结论只对 `0xc0048203` 成立**：
   - `0xc0048203`：`r6 = 100*table[hz]/5`，循环 10 轮 → 有上限，返回 ETIME。
   - `0xc004820d`：`ldr r1,=0x00989680`（字面量池 `*0x12ec` 实测 = 0x989680 = **10^7 jiffies**，HZ=100 时 ≈27.7 小时）。
   - `0xc0048211`：`down_interruptible`，**无超时**。

### 6.3 对 condvar-analyst 结论的补充（不冲突，且能补上一块）

- 我这边可证实：`grm_init` @0x11683c **静态 `bl` 图完全不连通** `rm_reg_init` / 任一个 ioctl 包装
  （BFS 从 0x11683c 出发 50 个节点、从 0x114dd0 出发 3 个、从 0x116800 出发 1 个，一个都到不了 0x12xxxx）。
  即 **DMP/GRM 上层到设备层是函数指针表间接调用的**，与"DMP 区没有 ioctl@plt"完全自洽。
- 我这边可证实：`sugilite_interrupt`（0x640）**只 `wake_up(&dev+0x4c)`，从不 `complete_all`**。
  所以 `0xc004820d` 的完成**必须**由同进程另一线程调 `0xc004820e` 触发 —— 即事件机制是纯软件的，
  这进一步支持"卡点在 libObj 自己的线程/锁，而非驱动阻塞"这个方向。

### 6.4 新增的运行时硬证据：全进程只有 1 条线程在驱动里

对 `diag7/11/12/13/14.txt` 全部采样点做统计，**每次采样都恰好只有 1 条线程 `wchan=sugilite_ioctl`**：

```
diag7.txt   5 次   pid 191 comm=THREAD_INFRA0
diag11.txt  5 次   pid 191 comm=THREAD_INFRA0
diag12.txt  5 次   pid 191 comm=THREAD_INFRA0
diag13.txt  5 次   pid 191 comm=THREAD_INFRA0
diag14.txt 12 次   pid 184 comm=THREAD_INFRA0
```

且 diag14 最后一次快照里，**所有 GRM/RM 命名的线程都是空闲的**（不在驱动里）：

```
183 grm_dn_vdf_if   osal_rcv_msg_tmo     ← GRM 显示线程，等消息
208 IMCMN-DRV-RM    futex_wait_queue_me
223 IMD-RM          futex_wait_queue_me
184 THREAD_INFRA0   sugilite_ioctl       ← 唯一一条，永久
173/176/177 THREAD_INFRA0  futex_wait_queue_me
169 im.real         futex_wait_queue_me
```

两条推论：

1. `THREAD_INFRA` 这个名字**在 libObj.so 里不存在**（`grep THREAD_INFRA libObj.so` 无命中，是 Infra/MWF
   线程池的名字），而 `grm_dn_vdf_if` **在** libObj.so @0x2a0465（是 libObj 自己 `pthread_create` 的线程）。
   `pthread_create` 出来的线程不设名时会继承父线程 comm（这里是 `im.real`），所以
   **卡在驱动里的那条是 Infra 任务线程池的 worker，不是 libObj 自己造的 GRM/kick 线程**。
   **已定案**：`libObj.so` 的 dynsym 里**没有 `pthread_setname_np`**，`prctl@plt` 全库只有 1 处调用
   （`0x1323ee`，`r0=16` = `PR_SET_DUMPABLE`，**不是** `PR_SET_NAME=15`）。
   ⇒ libObj 从不给自己的线程命名 ⇒ 它的 worker 的 comm **完全取决于创建它的那条线程**。
   ⚠️ **修正（condvar-analyst 指出，我原推论有漏洞）**：`comm` 继承的是**创建者线程**，不是进程。
   我原先写"必然是 `im.real`"是**建立在'创建者=主线程'这个未经证明的假设上**。
   若 `DMP_2D_Initialize`（→`0x181150`→`pthread_create`）是被某个 `THREAD_INFRA0` 任务线程调起来的，
   那么 W1/W2 的 comm **天生就是 `THREAD_INFRA0`**。所以 **184/191 到底是不是 DMP worker，静态定不了**，
   我撤回"一定不是"的强断言，改为**未定**，并倾向"很可能就是"（见 §6.7 的路径证据）。
   静态能给的唯一硬判据是**抓用户态返回地址**：出现 `0x18168C`（`0x181688 blx r3` 的返回地址，
   只可能出现在 worker `0x1815E0` 里）就是 DMP worker；出现 `0x12d172` 就说明卡在 `0x12d150` 的
   `ioctl(0xC0048211)` 里。两个同时出现 ⇒ 就是"DMP worker 卡在 mlock gain"。
2. 结合"`0xc004820d` 的唯一调用者 `0x12d090` 只被 kick 线程 `0x12d280` 调用"，
   而 kick 线程不是 Infra 线程 ⇒ **卡点更可能是 `0xc0048211`（mlock gain，无超时）或 `0xc0048203` 的某次重试**，
   而不是 `0xc004820d`。这与 §1.4 里"优先怀疑 0x0D"的初判**相反**，请以 §4.6 的 ioctl trace 定案。

### 6.5 `0xC0048211` 的等待与释放配对（结论：**纯用户态锁，造硬件没用**）

#### 6.5.1 驱动侧完整分支（`sugilite_ioctl` 0x8e4，`cmd == 0xc0048211` → 0x115c）

```
115c: mov   r2, sp
1160: mov   r0, r5            ; r5 = arg (用户指针)
1164: bic   r3, r2, #0x1fc0   ; ┐ current_thread_info()
1168: bic   r3, r3, #0x3f     ; ┘
116c: ldr   r1, [r3, #8]      ; addr_limit
1170: sub   r1, r1, #1
1174: bl    __get_user_4       ; r2 = *arg  = idx
1178: cmp   r0, #0 ; bne 12a8  ; 取失败 → -EFAULT(-13)
1180: cmp   r2, #2
1184: bhi   12a0              ; idx > 2 → mvn r4,#21 = -EINVAL(-22)
1188: mov   r3, #20
118c: mla   r4, r3, r2, r4     ; r4 = dev + idx*20
1190: add   r0, r4, #176       ; &dev[idx*20 + 0xb0]
1194: bl    down_interruptible ; ← 唯一的 down
1198: ...                      ; put_user(r0)  把 down 的返回值写回 *arg
```

- **信号量地址 = `dev + 0xb0 + idx*0x14`**（`0xb0 / 0xc4 / 0xd8`，共 3 把，idx ∈ {0,1,2}）。
  结构体 20 字节 = PREEMPT_RT 下的 `struct semaphore`
  （`+0x00..0x07` rt-mutex、`+0x08` count、`+0x0c/+0x10` wait_list 自环）。
- **初始值 count = 1**，在 `sugilite_open` 的"首个打开者"路径里初始化：
  `0x4ec memset(dev+0xb0,0,20)` / `0x4fc str r5(=1),[dev+0xb8]` / `0x500-0x504 self-list`，
  三把分别在 `0xb0 / 0xc4 / 0xd8`，形状完全一样。
  ⇒ **是二值信号量（互斥锁），不是计数信号量**。
- **写回给用户的值**：`0` = 成功拿到锁；`-EINTR(-4)` = 被信号打断。
  libObj 的封装 `0x12d150` 会**循环重试直到拿回 0**：
  ```
  12d16c: str r6,[r7,#4]      ; local = idx
  12d16e: ioctl(0xc0048211, &local)
  12d172: cbnz r0 → 打印 "mlock gain failed"
  12d174: ldr r0,[r7,#4]
  12d176: cmp r0,#0
  12d178: bne 12d160          ; != 0 → 再试
  ```

#### 6.5.2 谁 up？（**关键**）

对 `grm_gles.ko` 全模块搜 `down_interruptible` / `up` 的 `R_ARM_CALL`，**各只有 1 处**：

```
ko_grm_gles_r.dis:1333:  1194: R_ARM_CALL  down_interruptible     ← 就是上面的 0xC0048211
ko_grm_gles_r.dis:1360:  11f4: R_ARM_CALL  up                     ← 就是 0xC0048212
```

`0xC0048212` 的分支（0x11bc）：

```
11bc: get_user_4 → r2 = idx
11e0: cmp r2,#2 ; bhi → -EINVAL
11e8: mov r3,#20 ; mla r4,r3,r2,r4
11f0: add r0,r4,#176
11f4: bl  up
11f8: ... put_user(0)
```

⇒ 回答三个子问题：

| 问 | 答（硬证据） |
|---|---|
| down 的是哪个信号量 | `dev + 0xb0 + idx*0x14`，idx∈{0,1,2}，初值 1 |
| 谁 up | **只有 ioctl `0xC0048212`**。`sugilite_interrupt` 里没有 `up`；`sugilite_open`/`release` 也不 up（它们只在首开时 `sema_init` 一次）。**IRQ 76 / 硬件完全不参与** |
| 返回 0 的条件 | `down_interruptible` 真正拿到锁 → `*arg = 0`；被打断 → `*arg = -EINTR`，用户态重试 |

**⇒ 造 GPE 假设备对这条锁一点用都没有。** 这是"用户在 App 里自己配对的一把互斥锁，内核只当保管员"。
拿不到 = 别的线程拿了 `0xC0048212` 没还（或还之前自己先阻塞/退出了）。

#### 6.5.3 用到哪把锁（静态分布）

| idx | gain(0x11) 次数 | release(0x12) 次数 | 备注 |
|---|---|---|---|
| 0 | 0 | 0 | 从未使用 |
| **1** | **21** | 20 | 主力锁，遍布 rm 消息层（0x12bxxx / 0x12d4xx / 0x12d7xx / 0x12dxxx / 0x12e0xx–0x12ecxx） |
| 2 | 4 | 5 | `0x12d282`(kick 线程，2 处)、`0x12d410`、`0x12eb20` 等 |

kick 线程 `0x12d282` 的持锁窗口（重要）：

```
12d290: mlock_gain(2)          ← 进循环先拿 idx=2
12d294: ret = pop_msg(&msg)
12d2c4: mlock_release(2)       ← 处理完才放
12d2c8: wait_signal(0xC004820D)  ← 睡在这里（等 0x0E）
```
⇒ **idx=2 在"取消息 → 分发"整段里被 kick 线程独占**。任何其他线程在此时要 `0xC0048211(idx=2)`
就会永久挂在 `down_interruptible` 上；而它一旦挂上，就再也不会去调 `0xC0048212`，
形成**单向死锁**，且 `wchan` 恰好是 `sugilite_ioctl`。

#### 6.5.4 关于 `0xC004820F`（**它不是"提交命令"**）

驱动分支 0x121c 全文：

```
121c: get_user_4 → r2 = val
1238: cmp r2,#1 ; cmpls r0,#0 ; bne 12a8   ; val 必须 ∈ {0,1}，否则 -EINVAL
124c: ldr r1, [r4,#0xf8]     ; r1 = BAR = 0xF1000000
1250: orr r0, r2, #0x20000000
1254: str r2, [r4,#0xf0]     ; dev->[0xf0] = val
1258: add r1, r1, #0xc0
125c: bl  sugilite_iowrite32 ; iowrite32(val | 0x20000000, BAR+0xC0)
```

- **入参只能是 0 或 1** → 这是一个**开关位**，不是命令描述符。
- libObj 侧只有 2 个调用点，都是常量：
  `0x12cf80: movs r0,#1; bl 0x12c2b0` 和 `0x12cf90: movs r0,#0; bl 0x12c2b0`
  （所在函数 `0x12cd40`，错误串 `"can't set hw bypass feature."` @0x2a19fc，
   结果字符串 `"enabled"` / `"disabled"`）。
- ⇒ **`0xC004820F` = "设置 hw bypass 特性（0/1）"，语义是配置位，不是 doorbell/提交。**
  我之前用"doorbell"这个词有误导，改正：`BAR+0xC0` 是**控制寄存器**，`0x20000000` 是写入时的使能位。
  "用户态直写 BAR 下发命令"和"驱动代写 doorbell"两种说法里，**只有前者成立**；
  驱动的 `iowrite32` 仅出现在 ISR 的 ack（`+0x1c/+0x3c/+0x44/+0xc18`）和这个 bypass 位（`+0xC0`）。

### 6.6 `0xC004820E` 调用点清单（结论：**有 11 个，不是零**）

`0xC004820E` 的 **ioctl 调用点全库只有 1 个**：`0x12d110`，位于封装函数 `0x12d0f0` 内（`push {r4,r7,lr}` @0x12d0f0）。
而 `0x12d0f0` 本身被 **11 处**调用：

| # | 调用点 | 所在函数 | 触发条件（调用点前 10 条指令推出） |
|---|---|---|---|
| 1 | `0x12b53e` | `0x12af60` | 消息状态 `tst r2,#2` 且 `msg->[8] != gettid()` → 把事件回给**请求方** |
| 2 | `0x12b6ca` | `0x12af60` | 同上（另一分支，`msg->[0x44]` 作为 key） |
| 3 | `0x12b720` | `0x12af60` | 同上 |
| 4 | `0x12b7b8` | `0x12af60` | 同上 |
| 5 | `0x12b80a` | `0x12af60` | 同上 |
| 6 | `0x12b89a` | `0x12af60` | 同上 |
| 7 | `0x12d2aa` | `0x12d282`（kick 线程） | 取到消息且 `msg->[8] != 0` 且 `msg->[8] != gettid()` → `send_signal(msg->[8])` |
| 8 | `0x12d3b6` | `0x12d3b0`（2 条指令的薄壳：`movs r0,#0`） | **无条件 `send_signal(0)`**＝唤醒"自己这条线程"的事件（driver 里 key=0 → 用 `current->[0x108]`） |
| 9 | `0x12d42a` | `0x12d410` | `mlock_gain(2) → resume(0x09) → mlock_release(2) → …` 之后无条件发一次 |
| 10 | `0x12e6bc` | `0x12e5b0` | 同 #1（`tst r2,#2` + `!= gettid()`） |
| 11 | `0x12e702` | `0x12e5b0` | 同 #1 |

共同模式：**从消息体里取"请求方 key"（`msg+8` 或 `msg+0x44`），用 `0x12e590`（疑似 `gettid()`）
与之比较，不相等才 `send_signal(key)`**。这与驱动里"key 0 → 用当前任务 key"的语义完全自洽。

⇒ **"0x0E 零调用点"不成立**，事件投递者（requester→waiter 的完成通知）在代码里是存在的。
⇒ 但 **`0x0D` 仍然不是本次卡点**：它的唯一包装 `0x12d090` 只被 kick 线程 `0x12d282`
（`0x12d2c8`/`0x12d2f0`）调用，而 kick 线程是 libObj `pthread_create` 的、comm 必为 `im.real`；
日志里卡住的却是 `THREAD_INFRA0`（§6.4）。

#### 6.6.1 12 条 cmd × 调用点 × 所在函数（供对调用树）

| cmd | ioctl 调用点 | 所在函数（prologue 起点） | 语义 |
|---|---|---|---|
| `0xc0048202` | `0x12c46c` | `0x12c3c0` rm_reg_init | 取寄存器窗口物理基址（idx=1→0xF1000000） |
| `0xc0048203` | `0x12cc58` | `0x12cc00` | **等中断状态位**（≤10 轮超时） |
| `0x80048206` | `0x12c44e` | `0x12c3c0` rm_reg_init | 取窗口大小（0x4000） |
| `0xc0048208` | `0x12c6d6` | `0x12c6b0` | suspend（clock down） |
| `0xc0048209` | `0x12c736` | `0x12c710` | resume（clock up） |
| `0x8004820a` | `0x12c796` | `0x12c770` | 查 is-suspended |
| `0xc004820d` | `0x12d0b4` | `0x12d090` | **等事件**（10^7 jiffies） |
| `0xc004820e` | `0x12d110` | `0x12d0f0` | 发事件 |
| `0xc004820f` | `0x12c2d0` | `0x12c2b0` | 设 hw bypass（0/1）→ `BAR+0xC0` |
| `0xc0048211` | `0x12d16e` | `0x12d150` | **mlock gain**（down_interruptible） |
| `0xc0048212` | `0x12d1e0` | `0x12d1c0` | mlock release（up） |
| `0x80048263` | `0x12c582` | `0x12c3c0` rm_reg_init | 读驱动全局标志（PSC dummywrite） |

全部 12 个调用点集中在 **6 个函数**里，且**没有一个是 `0x181700` / `0x182160` / `0x1810F0`**
（那三个在 0x18xxxx，与 rm 层 0x12xxxx 之间只能靠函数指针表间接到达）。
⇒ 若要在调用树里对，只能在**运行时**看栈，静态对不上。

#### 6.6.2 不用 LD_PRELOAD 的 trace 办法（推荐，零改动）

`/proc/<tid>/syscall` 对**正在内核里睡着**的任务也返回进入系统调用时的寄存器快照：

```
# 先找那条线程
for t in /proc/<pid>/task/*; do
  echo "$t $(cat $t/comm) $(cat $t/wchan 2>/dev/null)"
done | grep -i sugilite

# 再直接读它的 ioctl 参数
cat /proc/<tid>/syscall
# ARM EABI: ioctl = 54 ; 输出格式 "54 <fd> <cmd> <arg> <a3> <a4> <a5> <sp> <pc>"
#                          第 2 个数就是 cmd，例如 0xc0048211
```

- 不依赖 preload、不依赖改 `.ko`、不改 libObj，一条 `cat` 就能定案。
- 顺带还能拿到 `fd`（对一下是不是 /dev/dmpgles2 的 fd）和 `arg`（**`*arg` 就是 idx，能直接告诉你卡的是 idx=1 还是 2**）。
- 若 busybox 的精简 `/proc` 没这个文件，退而求其次：`cat /proc/<tid>/stat` 的第 30 字段（`kstkeip`）
  加上 `wchan` 一起看；再不行才考虑 QEMU 侧 hook 内核 `sugilite_ioctl` 入口。

### 6.7 DMP 2D 路径 → `0xC0048211`：跨层桥找到了（独立复核 condvar-analyst 的路径）

我之前说"`0x18xxxx` 到 `0x12xxxx` 静态补不上"——**只说对了一半**：
`grm_init`（`0x11683c`，只管初始化）确实不连通；但 **DMP 的命令分发/绘制路径是连通的**，
桥在一个 `bl` 上。我用独立的 BFS 复现了同一条路径（`limit 200k`，889k 行全量）：

```
0x181240  (DMP 命令分发器)
  └─0x184ee0
      └─0x184de0                        ← 2D 绘制适配层
          └─0x184e68: bl 0x12bdb0       ← ★跨层桥（DMP → rm/GRM）
              └─0x12bdb4: bl 0x12dbb0   ← rm 侧包装
                  └─0x12dbbe: bl 0x12d150  ← mlock gain
                      └─0x12d16e: blx ioctl@plt   cmd = 0xC0048211
                          └─0x12d172  ← 返回地址（若线程停在这里，栈里就有这个地址）
```

同一路径还能到：
- `0x12d1c0`（`0xC0048212` mlock release）——**说明这条路上 gain/release 是配对写的**
- `0x12d3b0 → 0x12d0f0`（`send_signal(0)` = `0xC004820E`）——**说明这条路上事件也发**

`0x184de0` 体内对 rm 层的全部跨层调用（含辅助函数）：

| 调用点 | 目标 | 作用（推断） |
|---|---|---|
| `0x184e08` / `0x184e10` | `0x1306e0` | 取状态/计数 |
| `0x184e42` | `0x12f620` | 取当前 tid/key |
| `0x184e48` | `0x12f770` | 同上（另一 key） |
| **`0x184e68`** | **`0x12bdb0`** | **真正的 HW 提交入口（内部先 `mlock_gain(1)`）** |
| `0x184eb6` / `0x184ef6` | `0x16b7f0` | 上层公共函数 |

失败返回值：`0x184e78` 把非 0 映射成 `0xFFFFD8F1`（= **-9999**，厂商自定义错误码）。

**⇒ 结论修正**：静态上"DMP 绘一次图 = 走一次 `ioctl(0xC0048211)` + `ioctl(0xC0048212)`"是**成立**的。
所以 `THREAD_INFRA0` 卡在 `sugilite_ioctl` 与"DMP worker 卡在 mlock gain"**完全可以同时为真**（§6.4 已撤回强断言）。

### 6.8 还剩的缺口：`0x12dbb0` 里那把 idx=1 的锁，谁没还

- `0x12dbb0` 是"取一个 rqueue 槽位"的大函数（`0x12dbb0`–`0x12e010`，遇到 `0x320`=800 上限会失败返回）。
- 它自己 `gain(1)` 一次、`release(1)` 两次（`gainrel` 统计），**自身不嵌套**。
- 它调用链上会不会再碰到一次 `gain(1)`（同一线程递归 down 非递归信号量 ⇒ 立即自死锁），
  以及 21 个 `gain(1)` 站点里谁在异常路径上直接 `return` 而不 `release`，
  **静态只能给出候选、不能定案**——`gainrel` 的输出（`work/lock_pair.py gainrel`）里
  `@0x12e320 / @0x12e3d0 / @0x12eb20 / @0x12ebb0` 四个函数**只有 gain 没有 release**，
  但我的函数边界是启发式的（会漏判尾调用/epilogue 合并），**不能据此断言是 bug**。
- 真正的判据只能是运行时：`cat /proc/<tid>/syscall` 拿 cmd + `*arg`（idx），
  再抓一次用户态栈看返回地址是 `0x12d172` 还是 `0x18168C`（§6.6.2 / §6.4）。

## 7. BAR+0x1800 / +0x1804：命令提交口（A′ 实验 dump 的解释）

### 7.1 驱动从不碰 0x1800/0x1804

把 `grm_gles.ko` 里**全部** `sugilite_iowrite32` 调用点（22 个）用到的偏移列出来，集合是：

```
0x04  0x1c  0x34  0x3c  0x44  0x50  0x54  0xc0  0xc10  0xc18
0x1000  0x1080  0x10c0  0x1100  0x1114  0x1118  0x1410
```

**没有 0x1800、也没有 0x1804。** 全模块也搜不到 `#6144`/`#6148` 立即数。
⇒ **`BAR+0x1800` / `+0x1804` 是用户态直接 MMIO 写的**，与 §2.1 的"直写 BAR"结论一致。

### 7.2 写它们的是 libObj 的 `0x12cb60`（**这就是提交函数**）

```
12cb60: push {r4,r5,r7,lr}
12cb62: mov  r4, r0                 ; r0 = 提交参数块
12cb64: add  r7, sp, #0
12cb66: movw r0, #0x1804
12cb6a: ldr  r1, [r4, #4]           ; ← 参数块 +4
12cb6c: bl   0x12c990               ;   *(u32*)(regs + 0x1804) = arg[4]
12cb70: mov.w r0, #0x1800
12cb74: ldr  r1, [r4, #8]           ; ← 参数块 +8
12cb7a: b.w  0x12c990               ;   *(u32*)(regs + 0x1800) = arg[8]  （尾调用）
```

**写入顺序：先 `+0x1804`，后 `+0x1800`** ⇒ `+0x1800` 是**最后写的那个 = 触发/门铃**，
`+0x1804` 是**先摆好的载荷指针**。与你 dump 到的 `+0x1804=0x2056e270` / `+0x1800=4` 完全吻合。

`0x12c990` 是带门控的写寄存器原语：

```
12c99a: r2 = *g_flag            ; 一个全局字节
12c99e: r3 = (r2 != 0)
12c9a4: cmp r0, #0x1800
12c9a8: 若 offset != 0x1800 → r3 |= 1
12c9ae: if (r3 == 0) 跳过写入     ; ★只有 "g_flag==0 且 offset==0x1800" 时才不写
12c9b6: *(u32*)(g_regs_base + offset) = value
12c9c0: tail-call 0x12c8a0(…, 15, 2)   ; 日志/追踪钩子
```
⇒ **`+0x1800` 的写入被 `g_flag` 门控**（flag=0 就不触发命令）。这解释了"A′ 里 0x1800 有值但机器不动"的另一种可能：
先确认那个 flag 字节是 1。它在 `0x12c9c4` 的字面量池里（PC 相对），运行时可在 libObj 数据段定位。

### 7.3 谁调它（2 处），以及提交时的上下文

| 调用点 | 所在函数 | 说明 |
|---|---|---|
| **`0x12b244`** | `0x12af60` | **真正的 2D 提交路径** |
| `0x12cfb2` | `0x12cd40` | resume 后的重放路径 |

`0x12b244` 现场（前后 20 条，全部为指令级证据）：

```
12b20e: ldr.w r0, [r7, #0x1e0]      ; ← 描述符物理地址（你 dump 到的 0x2056e270）
12b212: ldr.w r2, [r7, #0x1e4]      ; ← 条数/长度（你 dump 到的 4）
12b216: movs  r1, #1
12b218: str   r1, [r7, #0x3c]
12b21a: str   r0, [r7, #0x7c]       ; 参数块+4  = desc_phys
12b21c: str   r1, [r7, #0x40]
12b21e: str.w r2, [r7, #0x80]       ; 参数块+8  = count
12b222: bl    0x12f620              ; 取当前 tid/key
12b22a: str   r0, [r7, #0x44]
12b22c-12b238: 比较某个标志 == 2
12b23c: bl    0x12e7f0
12b240: add.w r0, r7, #0x78         ; 参数块 = { +0x00: ?, +0x04: desc_phys, +0x08: count }
12b244: bl    0x12cb60              ; ★SUBMIT（写 0x1804 / 0x1800）
12b248: movs  r0, #1
12b24a: bl    0x12cc00              ; ★立刻 ioctl(0xC0048203) 等中断，通道号 = 1
12b24e: tst.w r0, #0x01000000       ; ★检查 bit24
12b252: bne.w 0x12b750              ; 成功分支
12b256: …                           ; 失败分支（bl 0x12e7e0 之后继续）
```

**⇒ 硬件事务是成对的：`写 +0x1804/+0x1800（kick）` → `ioctl 0xC0048203` 等 `mask & 0x01000000`。**
这就是 §1.4 说"要在 0xF1000000 造设备"的具体握手点：
QEMU 只需在 `+0x1800` 被写之后，让 `+0x34` 读回带 `0x04000000`（ISR 识别的位，会 ack 并唤醒），
且让 `0xC0048203` 返回的掩码里 `0x01000000` 置起 —— 上层就走成功分支。

### 7.4 描述符帧结构的字段（部分）

提交前 `r8` 指向的那块帧结构被写了这些偏移（`0x12b1c6`–`0x12b206`）：

| 偏移 | 值/来源 |
|---|---|
| `+0x5c` | `(neg(height) & 0xffff) \| (width << 16)` |
| `+0x64` / `+0x6c` / `+0xec` | `(height & 0xffff) \| (width << 16)` 同族打包 |
| `+0xe4` | `channel << 16`（`channel == 5` 时改成 25） |
| `+0x294` / `+0x29c` | 8 字节对齐后的 pitch/stride 组合 |
| `+0x2ac` | `pitch >> 3` |
| `+0x2b4` | `channel`（5→25） |
| `+0x64c` | `width >> 3` |

另有一组浮点运算对 `width/height` 乘 0.5（`vmul s14,s14,#0.5` @0x12b160/0x12b16a）存到 `0x1dc/0x1d8`，
典型的**居中/旋转原点**计算。

⚠️ **未确定**：`[r7+0x1e0]`（描述符物理地址）本身的构造点还没定位到 ——
它由 `0x12b120: bl 0x130230` 与 `0x12b13e: bl 0x12e800`（`&r7+0x1e4`）附近填写，
但真正 memcpy 描述符内容的那段还没读通。**你 dump 到 1KB 后贴给我，我可以按 §7.4 的字段表逐字对。**

### 7.5 你问的另外三个字

`+0x0004=0x00070100`、`+0x001c=0x00010000`，以及 `+0x1080/0x1114/0x1118/0x00c0`，
**全部是 `sugilite_register_init`（`0x54`，`dev_init` 与 `clock_up` 都会调）写的静态寄存器表**，
和你 dump 到的值一字不差：

| 偏移 | 写入值 | 指令 |
|---|---|---|
| `+0x0004` | `0x00070100` | `0x14c ldr r1,[r4,#4]`；`0x150 ldr r0,=0x00070100`（pool `0x1a4`） |
| `+0x001c` | `0x00010000` | `0x170 add r1,r4,#28`；`0x174 mov r0,#65536` |
| `+0x1080` | `0x12345678` | `0x98 add r1,r4,#4224`；`0x94 ldr r0,=<0x190>` |
| `+0x1114` | `0x76543210` | `0xe4 add r1,r5,#20`；`0xe0 ldr r0,=<0x194>` |
| `+0x1118` | `0xfedcba98` | `0xf0 add r1,r5,#24`；`0xec ldr r0,=<0x198>` |
| `+0x00c0` | `dev->[0xf0] \| 0x20000000` | `0x180 add r1,r4,#192`；`0x184 orr r0,#0x20000000` |

⇒ `0x12345678 / 0x76543210 / 0xfedcba98` 是**驱动自检常量**（写完可能回读校验），
`+0x04 / +0x1c` 是引擎的基础配置位；`+0xc0` 就是 §6.5.4 的 bypass 位（val=0 → 只留 `0x20000000` 使能位）。

### 7.6 用户态还会写 `0xC00`–`0xC28` 一族配置寄存器（含 ack 寄存器 `0xC18`）

`0x12c9d0` 是一个**寄存器编程例程**（不是读状态），它按序写：

```
0xC00  0xC04  0xC10  0xC20  0xC24  0xC28
0xC04  0xC08  0xC0C  0xC10          （第二条路径）
0xC18  × 3 处：0x12ca3c（写 1）/ 0x12cb10 / 0x12ccd8（值不定，可能为 0）
```

**⚠️ 对 QEMU 建模的含义**：`0xC18` 在驱动里是"bit30 中断的 ack"，但**用户态也把它当配置寄存器写**。
如果假设备用"`off==0xC18 && val==0` → 清 int_status"的规则，遇到后两处写 0 时**会把挂着的完成信号误清**，
表现为"raise 有、ack 有、线程仍不动"，极易被误判成锁问题。
⇒ 本固件的模型只需认 **`off==0x1C && val==0`** 这一条 ack（因为只置 bit26，ISR 的 `tst bit30` 恒假、永不写 `0xC18`）。

**另一处自我更正**：`0x12cc62 movs r0,#0x34 ; bl 0x12c8a0` **不是**对 `0x34` 的 dummy write。
`0x12c8a0` 是**日志/追踪钩子**（`ldrb r2,[r5]` 读全局开关，`cmp #1` 不等即返回，等才 `blx` 函数指针），
四个参数是日志参数。⇒ 正常运行时**不会有 `GPEW off=0x34`**；若出现即为异常。

### 6.9 `0xC004820D` 第一次调用也会真阻塞（重要修正）

qemu-model-auditor 曾假设"sugilite_open 的 `complete_all` 让第一次 `0x820D` 立即返回"。
**这个假设不成立**，原因是一条容易看漏的 pre-index 写回：

```
f64: ldr  r4, [r6, #16]        ; r4 = node->[0x10]  ← pending 闭锁（不是 completion->done）
f68: cmp  r4, #0
f6c: bne  fa4                  ; 闭锁非 0 → 立即返回 1
f70: ldr  r0, =event_list_lock
f74: str  r4, [r6, #20]!       ; ★ node->[0x14] = r4 = 0  （此处 r4 必为 0）
f78: bl   rt_spin_unlock
f80: mov  r0, r6               ; &node->[0x14]
f88: bl   wait_for_completion_interruptible_timeout
```

`open` 里 `0x5DC bl complete_all` 确实把 `done` 置非 0，**但 `0xF74` 每次等之前都把它清 0**。
真正决定"立即返回"的是 `node->[0x10]` 这个 **pending 闭锁**，而它在建节点时被显式清 0
（`0x5D0 str r7,[r4,#16]`，新建路径下 `r7 == 0`）。闭锁只有 `0xC004820E` 路径 A
在 `completion_done()==真` 时才会置 1（`0x1138 movne r3,#1 ; 0x113C strne r3,[r6,#0x10]`）。

⇒ **kick 线程（`0x12D280`）在它的第一次 `0xC004820D` 就会阻塞**，不需要"先被消费过一次"。
这与硬件完全无关，是"事件 POST 者（`0xC004820E`）没被调用"的直接后果。

**同时修正一处措辞**：event key 不是 per-thread。`open` 全进程只调一次、只建一个 node，
而 kick 线程也拿它调 `0x820D`（否则 `get_event` 返回 NULL 会走 `0xF58 bl __bug_noarg`），
所以 `[task+0x108]` 必然是**进程级共享字段**（tgid 之类）。
⇒ **一个进程一个 event node，所有线程共享**；这也解释了为什么 `0x0E` 的 key 能从消息体里取别人的值。

### 6.10 `0xC004820E` 两条子路径的精确归属

`0x10AC` 与 `0x1148` 这两处 `complete_all` **都属于 `cmd == 0xC004820E`**：

| 地址 | 路径 | 判定 | 动作 |
|---|---|---|---|
| `0x10AC` | B（`0x102C` 起，`key==0`） | `get_event(key==0 ? current[+0x108] : key)` | `complete_all(&node->0x14)` |
| `0x1148` | A（`0x10EC` 起） | `completion_done(&node->0x14)` | 真→`node->[0x10]=1`（置闭锁）；假→`complete_all` |

---

## 附：本次产出的辅助脚本（都在 `work/`）

| 脚本 | 作用 |
|---|---|
| `aio_dis_ko.sh` / `aio_dis_ko2.sh` | 反汇编 grm_gles.ko（含 `-dr` 重定位）、导符号/字符串 |
| `xref_multi.py` | 按"ldr+add pc"还原 PIC 字符串 xref（支持批量目标） |
| `slice_dis.py` | 从 `libObj.dis` 按地址区间切片（带最近符号名） |
| `scan_ioctl2.py` | 扫全库 40 个 `ioctl@plt` 调用点，回溯 request 号并解 `_IOC` |
| `callgraph.py` | 静态调用图 BFS（用于判定"是否可达"） |
| `lock_pair.py` | `show <addr>` 列某函数的全部调用点+前置条件；`gainrel` 统计 mlock 获取/释放配对 |

生成物：`ko_grm_gles_r.dis`、`ko_gles_ioctl_r.txt`、`ko_gles_rodata.txt`、`libObj.dynsym.txt`。

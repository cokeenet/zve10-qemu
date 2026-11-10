# "agent 不在听 7777" ≠ "agent 没运行" —— 一个关键的区分

## 一、先确认的事实

**用户指出：刷 hybrid 后起不来、刷回老菜单就正常 ⇒ 刷写确实生效。**
**这个推断成立，我之前"可能刷没进去"的猜测撤回。**

## 二、链路上各环节的闭包都完整

| 文件 | UND | 真实缺失 |
|---|---:|---:|
| `bootin.elf`（E10 版） | 47 | **0** ✅ |
| `bootin.elf`（M2 版） | 47 | **0** ✅ |
| `im.elf`（E10 版） | 64 | **0** ✅ |
| `im.elf`（M2 版） | 64 | **0** ✅ |

**⇒ `bootin.elf` → `im.elf` 这条最前面的链在符号层面没有障碍。**

**⇒ 那么 `im.elf` 应当能启动，`LD_PRELOAD=agent_v14.so` 应当被加载，
agent daemon 应当监听 7777。**

## 三、★ 为什么"连不上"不等于"没运行"

**agent 是通过 **WiFi** 访问的（<CAMERA_IP>，DHCP）。**

**而混合镜像的无线栈是**混搭**的：**

| 组件 | 混合镜像里 | E10 | M2 |
|---|---|---|---|
| `bcmdhd.ko`（驱动） | **E10 版** | 31204543 | eb3643c8 |
| `libWlanAdjust.so` | **E10 版** | fd09c3ae | 70ee3e18 |
| `libWlanResource.so` | **E10 版** | 3abf557f | 06d539e0 |
| `libWlanControlCommon.so` | ★ 需确认 | a7565f8f | 73669751 |
| **网络管理栈**（`libInfraNetConnectionManager` 等） | **M2 版** | — | — |

**⇒ 驱动是 E10 的，但上层网络管理是 M2 的。**

**⚠️ 如果 M2 的网络管理无法驱动 E10 的无线栈 ⇒ WiFi 根本起不来
⇒ agent 即使正常运行，也**不可达**。**

**⇒ 所以"读不到 7777"有两种可能，而此前只考虑了第一种：**
```
A. agent 没运行（im.elf 没起来）
B. ★ agent 在运行，但 WiFi 没起来（网络栈不匹配）
```

## 四、★ 决定性的廉价实验：**信标 preload**

**不需要网络、不需要 UI、完全可逆：**

```
1. 写一个极小的 .so，构造函数里只做一件事：
     open("/log/beacon.txt", O_WRONLY|O_CREAT|O_APPEND) + write("im.elf preload ran\n")
   （纯 syscall，不依赖 libc）
2. 放到 /log/beacon.so
3. /setting/mode/preload = /usr/tool/libefence.so:/log/beacon.so
   （同时把 agent 摘掉，避免干扰）
4. 刷 hybrid → 开机（预期无 UI）
5. 刷回老菜单 → 读 /log/beacon.txt
```

**判读：**

| beacon.txt | 结论 |
|---|---|
| **不存在** | **`im.elf` 根本没启动** ⇒ 问题在 bootin → im.elf 之间 |
| **存在 1 行** | `im.elf` 启动了 ⇒ 问题在 appFwBoot / appFw / CP 协议 |
| **存在多行** | 多个进程都加载了 preload |

**⇒ 这一步把"启动链断在哪"从猜测变成二值事实，且完全可回退（只动 /log 和一个 /setting 文件）。**

## 五、配套的第二个判读点

**同一实验里再加一个信标变体：把 beacon 做成在 `dlopen` 前后各写一行 ——
但那是 appFwBoot 内部的事，需要改 appFwBoot，成本高。**

**⇒ 先做上面那个最小版本，它已经能区分最大的两类原因。**

## 六、结论

**目前**不能**说"新菜单起不来是因为 CP 协议不匹配"，也**不能**说"内存不足"（已证伪）。**

**能说的是：**
```
✓ bootin/im.elf 闭包完整，最前面的链无符号障碍
✗ 不知道 im.elf 是否真的执行了
✗ 不知道 appFwBoot 是否被 dlopen 了
✗ 不知道 appFw 是否被 dlopen 了
⇒ 三件事都需要运行时证据，而 beacon 是最便宜的第一步
```

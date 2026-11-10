# 明天怎么做 —— 一页纸

## 目标
真机出现 ZV-1M2 新菜单 UI。

## 两个方案，先做 A（零风险），A 不行再做 B（刷机）

---

## 方案 A：loop + bind（推荐，不刷机，重启即恢复）

### 前提
- 相机正常开机、进老菜单、WiFi 已连
- 电脑在同网段（热点 `TestHotspot`，相机通常 <CAMERA_IP>）
- agent 在跑（`python3 /root/fw/oss/tools/payload/cam.py ping` → PONG）
  - 若没跑：插 USB 线 → `cd tools/payload && source env.sh && python3 send_payload.py vendor/agent_v14.so --pid 0x0D95` → 拔线等 WiFi

### 三步
```bash
cd /root/fw/oss/tools/bindm2

# 1) 看状态
python3 deploy_bindm2.py --status

# 2) 上传镜像（237.6 MB，238 × 1 MB，约 4~8 分钟）
python3 deploy_bindm2.py --image /mnt/build/nflasha15_v9.ext2

# 3) 重启相机
```

### 预期
- 屏幕出现 M2 新菜单 ⇒ 成功，进功能验证
- 仍老菜单 / 黑屏 ⇒ 看取证：
```bash
python3 deploy_bindm2.py --status          # 里面有 bindm2.log
# 或
python3 ../payload/cam.py run "cat /log/bindm2.log"
python3 ../payload/cam.py run "cat /proc/mounts | busybox grep m2root"
```
`bindm2.log` 每行是一个 syscall 的返回值：
```
LOOP_SET_FD=0     ← 0 成功；负数 = errno
mount ext2=0      ← -19 ENODEV 镜像不是 ext2 / -1 EPERM 没权限
bind /usr/lib=0
bind /usr/share=0
```

### 恢复（任何时候）
```bash
python3 deploy_bindm2.py --rollback     # 或直接重启，bind 天然不持久
```

### 失败后用自动诊断器（一步给出结论 + 下一步）
```bash
python3 diagnose.py
# 它会依次读：uptime / preload / 镜像大小 / bindm2.log / 挂载状态 / im.elf 映射
# 然后输出判决，例如：
#   ✓ bind 完全生效（6/6 关键文件都是 M2 尺寸）
#   ✓ appFw.so 已被映射 ⇒ 新 UI 代码已装载
#   → 若屏幕仍无 UI，问题在绘制/CP 层，不在装载层
# 或：
#   ✗ appFwBoot 装载了但 appFw 没有 ⇒ 卡在 Application_System_Init
```

---

## 方案 B：fastboot 刷 /usr

**（payload 结构、协议一致性、写入计划均已离线验证；预计 317 次 ≤768 KiB 写入，1~2 分钟）**

```bash
cd /root/fw/oss/fastboot

# 0) 先演练（不碰硬件）
python3 host/flash_partition.py --part nflasha15 \
  --image /mnt/build/nflasha15_v9.ext2 \
  --part-start 0x2D800000 --chunk-kb 768 --full --plan-only
#   期望: 3801 blocks = 237.6 MiB, 1 contiguous run, 317 writes

# 1) Pico 注入 stage1
python3 pico/inject_payload.py --port /dev/ttyACM0 --file prebuilt/stage1.bin --duration 600

# 2) 刷写（每 30 块校验一次）
python3 host/flash_partition.py --part nflasha15 \
  --image /mnt/build/nflasha15_v9.ext2 \
  --part-start 0x2D800000 --chunk-kb 768 --full --verify-every 30
```
恢复：刷回 `/root/fw/zve10_backup/nflasha15.img`（同样先加 `--plan-only` 演练）

**判据（刷完开机后）：**
```bash
ls -l /usr/share/app/global.xdb
#   12252 ⇒ 没刷进去（还是工厂）
#   16112 ⇒ 刷进去了（M2 资源就位）
```

---

## 出问题时的取证（不需要 agent 也能做）

```bash
# 把 /tmp 分区读出来看 beacon（/tmp 在 /usr 之前挂载，最可靠）
cd /root/fw/oss/tools/payload
python3 cam.py run "cat /tmp/.beacon.txt"     # 若 agent 能跑
# 若 agent 不能跑：用 fastboot 读 nflasha10（偏移 0x0AC00000）里的 .beacon.txt
```

beacon 每条记录含：
```
argv        → im.elf 收到什么参数
usr_mount   → /proc/mounts 里的 /usr 行
bootin_head → ELF(原厂) / SCRIPT(我们的)
memfree     → 剩余内存
```

---

## 当前静态分析状态（已完成，不用重做）

```
✅ 文件系统：e2fsck 干净、几何/UUID/特性位与原厂一致
✅ 符号闭包：im.elf / bootin / sen 只缺 2 个 CRT 弱符号；
             appFwBoot 缺 0；appFw 的 2 个缺口与 M2 原版完全相同
✅ 传递依赖：全镜像 224 个二进制零缺失
✅ 版本正确性：v9 里除硬件绑定层(kmod/sabin)与启动锚点外，业务库全部 M2 版
✅ 内存：两机型 Linux 可用均 141.7 MB（内存假说不成立）
✅ 资源：global.xdb 16112 / style_cmn.uxc 2004 / area_check_data_vertical 1M 全为 M2 版
✅ loop 驱动：内核有（"loop: module loaded" + loop_init + loop.max_loop）
✅ SONAME 顶替：两版 libIMDB 的 SONAME 相同、无版本冲突、构造函数无副作用
```

**⇒ 静态分析已做到极限。剩下的只能靠真机跑一次来验证。**

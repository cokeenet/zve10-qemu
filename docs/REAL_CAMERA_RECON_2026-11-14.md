# ZV-E10 真机侦察报告（10.42.0.158）

> 全部数据来自 `tools/payload/agent/cam.py` 的 PULL/PUSH/PING，只读或写入 /log、/setting。

## 一、这台相机的身份（关键）

```text
/usr/lib/libObj.so 前 8 MB 的 sha256 = 18bb9e6658d933dade6a268c…
本机 bundle-v2 的 ZV-E10 Gen4 libObj（完整 sha cfdc59b9…）前 8 MB = 完全相同
```

**⇒ 这台相机是 ZV-E10（Gen4，libObj = cfdc59b9…），不是 ZV-1M2 的 Gen5 库。**

**⇒ 原来那个"让 Gen5 库在 ZV-E10 上画菜单"的目标，在这台机器上不成立** ——
这台机器本身就是原目标里的"宿主模型"，不是"要移植的库"。

## 二、系统形态

```text
kernel       3.0.27_nl-rt106+，#1 SMP PREEMPT RT，Jun 5 2026
/            /dev/nflasha7   ext2  **ro**
/usr         /dev/nflasha15  ext2  **ro**
/tmp, /etc   /dev/nflasha10  vfat  rw（**实测写失败**）
/system      /dev/nflasha3   vfat  rw
/setting     /dev/nflasha2   vfat  rw  ✓ 实测可写
/log         /dev/nflasha11  vfat  rw  ✓ 实测可写
/tmp_ipsec   tmpfs           ✓ 实测可写
```

## 三、通道能力（把关）

| 操作 | 状态 | 说明 |
|---|---|---|
| PING | ✓ | |
| PULL | ✓ | 只读，单次 8 MB 上限 |
| PUSH | ✓ 部分 | **只有 /log、/setting、/tmp_ipsec 成功** |
| RUN | ✗ | agent 把临时文件写 `/tmp`，`/tmp` 写不了 ⇒ 恒返回 `ERR exec` |

**⇒ 这条通道没有 shell 能力（RUN 不可用），只有文件读写。**

## 四、已有的部署痕迹

```text
/etc/ld.so.preload        = /etc/zve10_agent.so   （agent v11 已装，uid=0）
/setting/mode/preload     = /usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so
/setting/mode/dmode       = 3
```

**注意：`/usr/lib/mprshim.so` 在这台机器上不存在（PULL 返回 ERR open）**
⇒ 白名单里有一个**失效项**。`bootin` 的 preload 白名单若有一项不存在，
是否会整串丢弃未验证（README 只说"内容必须以白名单前缀开头"）。

## 五、这对"真机出新菜单"意味着什么

**现在有了明确的三个事实：**

```text
① 真机是 Gen4（ZV-E10 原厂固件），它的原厂菜单本来就能出（本机 QEMU 已实测 19 帧）
② 通道只有文件读写，没有 shell ⇒ 无法用 RUN 做 bind/insmod/remount
③ /usr 是 ro ext2 且启动时被完整性还原 ⇒ 不能往 /usr 写东西
```

**可行的做法只剩一条（且需要您授权）：**

```text
用 /setting 分区 + bootin 的 preload 白名单机制注入一个 .so
  · /setting 可写 ✓
  · /setting/mode/preload 是 bootin 读的 LD_PRELOAD 白名单 ✓
  · 但需要：.so 文件放在白名单里的某个路径上（现在是 /usr/lib/mprshim.so，不存在）
  · 或：改 preload 内容为指向 /setting 下的 .so（需确认白名单是否允许非 /usr/tool 前缀）
```

## 六、我需要的确认

**要把这条路走通，必须先确认一件事：**

```text
bootin 的 preload 白名单是否允许 /setting 下的 .so？
  README 说"内容必须以白名单前缀开头"，否则整个 LD_PRELOAD 被丢弃
  ⇒ 白名单前缀是 /usr/tool/LeakTracer.so 这种
  ⇒ 若必须以此开头，则无法直接指向 /setting
  ⇒ 那就只能靠 mount --bind 把 /setting/x.so 挂到 /usr/tool/xxx.so 上
     而 bind 需要 shell（RUN 不可用）⇒ 只能靠 agent 自带的 bind.txt 机制
```

**agent 自带 `bind.txt` 机制（`/log/zv1f_ui/bind.txt`，每行 `源<TAB>目标`），
开机时自动 `mount --bind`。这条路径不需要 shell ⇒ 可用。**

⇒ **完整可行方案（不碰 eMMC，只写 /log + /setting）：**

```text
1. 编译一个 overlay .so（ARMv7 softfp，只依赖 libc）
   作用：hook 原厂 UI 的 present 提交点，把新菜单合成到 640×480 帧上
2. push 到 /setting/newmenu.so
3. push 一个 /log/zv1f_ui/bind.txt：
      /setting/newmenu.so<TAB>/usr/lib/newmenu.so
4. 重启相机 ⇒ agent 构造时 apply_binds(1) 自动 bind ⇒ .so 出现在 /usr/lib/newmenu.so
5. 改 /setting/mode/preload，把 /usr/lib/newmenu.so 加进白名单
6. 重启 ⇒ bootin 带着 LD_PRELOAD 拉起 im.elf ⇒ 新菜单生效
7. 回滚：删 /log/zv1f_ui/bind.txt + 改回 preload ⇒ 重启
```

**风险点（必须提前说）：**
```text
· 第 3 步需要 /log/zv1f_ui/ 目录存在（当前 PULL 显示不存在，需要先创建 —— 但 RUN 不可用，
  无法 mkdir ⇒ **这一步现在做不了**）
· 改 /setting/mode/preload 若格式错误 ⇒ bootin 丢弃整个 LD_PRELOAD（相机仍能开机，安全）
· .so 若有 bug ⇒ 每个进程加载它都会受影响（但只影响 im.elf，因为只有 bootin 带 preload）
```

## 七、结论

**这台真机可以做"新菜单"，但有三个硬约束：**
```text
① 不能碰 /usr（ro + 启动还原）
② 没有 shell（RUN 不可用）⇒ 所有操作必须靠 PUSH + 开机自动执行的机制
③ 必须复用 bootin 的 preload 白名单 + agent 的 bind.txt 机制
```

**而当前的直接阻碍是：`/log/zv1f_ui/` 目录不存在，而创建它需要 shell。**
⇒ **下一步必须解决"如何在没有 shell 的情况下建目录"。**

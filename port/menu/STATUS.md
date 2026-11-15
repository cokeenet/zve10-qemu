# ZV-E10 新菜单移植：当前状态（2026-11-15）

## 一、研究完成的部分

对 A6400-Extra-Menu 仓库的全部 77 个文件已逐文件读完，技术地图见
`docs/a6400-extra-menu/ANALYSIS.md`（442 行）。核心结论：

```text
✓ 它用索尼自己的 WidgetSystem 建 Layer+Panel+Label，不是自绘像素
✓ 19 个 API 里我们 ZV-E10 有 18 个（缺的那个有等价替代）
✓ Panel/Label 对象布局与 A6400 完全一致（构造写到 0x1f8 / 0x374）
✓ 注入点我们有更好的：InfraInput::SetCallback（不用改代码字节）
✓ 它全部只改 RAM，不改分区 —— 正合我们的约束
```

## 二、真机实测结果（本轮）

### 2.1 环境

```text
相机 ZV-E10 2.03，agent TCP 7777 可用（root，CapEff=ffffffff）
im.elf pid=157，线程 201 个（遇重启后变化）
bootin.elf pid=149
/tmp 可写（nflasha10 vfat），/log 可写（500MB），/usr 只读 ext2
**/tmp_bt 是只读的**（A6400 用它存 so，我们不能用）
```

### 2.2 ★ 关键实测：ptrace 可用但**极危险**

```text
用我写的 tids_test.elf 对 im.elf 的线程逐个 ATTACH/DETACH：
  · ATTACH 成功（PTRACE_AVAILABLE）
  · 但对 UI 线程 ATTACH 后，相机 **重启了**
    （uptime 从 157s 掉到 130s，随后恢复）

⇒ 结论：**A6400 的 ptrace 冻结方案在 ZV-E10 上会让相机重启**
⇒ 原因推测：im.elf 的 UI 线程被 STOP 后，watchdog 判超时 → 重启
⇒ **这条路在我们机器上不可用**
```

**这正好印证了我的判断：我们的 InfraInput::SetCallback 方案更安全。**

## 三、下一步（不碰 ptrace 的做法）

```text
方案改为：
① 写一个 .so，放在 /tmp（可写），由 LD_PRELOAD 或 dlopen 加载
② 在 .so 的构造函数里：
   - dlopen("libObj.so", RTLD_LAZY)
   - dlsym 拿 18 个 API
   - sys_getInstance() 拿单例
   - createLayer(...) 试建图层
③ 若成功 → 建 Panel/Label，setActive(1)，观察屏幕

不涉及：
✗ ptrace / 冻结线程 / 改代码字节
✗ 写 /usr / /system
✗ WBI
```

**阻碍：需要让 im.elf 加载我们的 so。可选路径：**
```text
A. /etc/ld.so.preload 加一行（改 /etc，nflasha10 可写，且是 A6400 的做法）
B. bootin 的 /setting/mode/preload 白名单（需 dmode 含 p，改 /setting）
C. 直接 dlopen 注入到运行中的 im.elf（需要 ptrace，已证明危险）
```

**⇒ 应走 A（改 /etc/ld.so.preload），这是唯一不碰 ptrace 的路径。**

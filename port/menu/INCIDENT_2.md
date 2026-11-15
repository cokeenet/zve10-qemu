# 事故 2：只杀 im.elf 也不行 —— 相机同样掉网

## 一、事实

```text
00:12 左右执行 /log/.relaunch.sh：
  1. kill -15 im.elf（**保留 bootin 149**）
  2. 等 4 秒
  3. 带 LD_PRELOAD + 完整环境，手动 /usr/bin/im.elf & 
  4. sleep 30
结果：agent 连接超时，相机从网络消失
```

## 二、关键判断

```text
之前我以为「杀 bootin 才断网」，现在证明：
**只杀 im.elf 也会断网**

⇒ 断网的真正原因不是 bootin，而是 **im.elf 本身**
   im.elf 是 UI 主进程，也可能是 Wi-Fi/网络服务的宿主或管理者
   （在它的 maps 里有大量网络库：
     libInfraNetConnectionManager.so、libInfraNetInitializer.so、
     libInfraNetDeviceInfo.so、libGOConnectionManager.so、
     libInfraRemote.so、libInfraHttpClient.so …）

⇒ 杀掉 im.elf = 杀掉网络管理 ⇒ Wi-Fi 断开
⇒ 而手动启动的 im.elf 可能因为缺少 bootin 的某些初始化（socket、设备节点、
   共享内存、与其他守护进程的 IPC）而无法重新接管网络
```

## 三、这意味着什么（重要）

```text
★ **任何"杀掉 im.elf 再手动起"的方案都会断网**
★ 而 agent 依赖 Wi-Fi ⇒ agent 也断
★ 所以：**不能在相机运行时替换 im.elf**

⇒ 唯一不杀 im.elf 的方式：
   **新增一个 im.elf 实例**（旧的继续跑）
   ⇒ 但两个 UI 抢屏幕/输入，风险高

⇒ 或者：**让我们的库进到已有的 im.elf 里**
   ⇒ 这需要 im.elf 启动时就带着 LD_PRELOAD
   ⇒ 也就是必须让它**重新启动**（bootin 的 fork_imelf）
   ⇒ 而重新启动 im.elf 一定会断网…
```

## 四、真正的解法只能有一个

```text
★ 接受"会断网一次"，但要做到：
   1. 断网后相机能自己恢复（通过 watchdog 重启 im.elf）
   2. 或者用 USB 连接（不依赖 Wi-Fi）继续操作

⇒ 关键问题：断网后 im.elf 会不会被自动重拉？
   实测：不会（曾经等 20 秒没重拉，最后靠拔电池）

⇒ 所以每次实验都要用户拔电池 ⇒ 代价太高
```

## 五、需要重新审视的前提

```text
也许我们一直在错误的路径上：
  · 把库预加载进 im.elf，就必须重启 im.elf
  · 重启 im.elf 就断网
  · 断网就无法继续

⇒ 那么"用 LD_PRELOAD 预加载"这条路本身代价过高。

替代思路：
  A) 用 /etc/ld.so.preload —— 但 im.elf 启动时 /etc 还没挂（已验证无效）
  B) 让 bootin 在 fork im.elf 时设置 LD_PRELOAD —— 就是 dmode=3p，但要重启
  C) ★ 改用 bootin 的 `lt` 选项（Leaktracer 模式）—— 它也会设 LD_PRELOAD，
       但可能有不同的加载时机
  D) ★★ 用 USB 连接替代 Wi-Fi（断网后仍能操作）
```

## 六、USB 连接的可能性

```text
Sony 相机有 USB 连接模式。如果断网后能通过 USB 进入某种 shell
（例如 PMCA / bk.elf / 服务模式），就不怕断网。
⇒ 需要验证：相机能否在"无 UI"状态下通过 USB 被识别
⇒ 这正是 A6400 项目用的 pmca-console 那条路
```

## 七、下一步

```text
1. 等相机回来（用户可能又要拔电池）
2. 立刻把 dmode/preload 回滚（避免下次开机又尝试加载 menu_ui.so）
3. 重新评估：是否需要转向 USB 路线
```

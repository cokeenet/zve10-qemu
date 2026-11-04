# 事故记录：看门狗级联重启相机

## 一、现象

**连续几次相机自行重启（用户报告 3 次），每次都是我在 agent 里试跑 `zvegisk.so` 之后。**

## 二、根因（我的代码 bug）

**`zvegisk.so` 的 `work()` 结尾无条件 fork 一个 `watchdog()`：**

```c
static void watchdog(void) {
    /* 睡 90 秒 */
    for(int i=0;i<90;i++) nanosleep(1s);
    if (ui_running()) { boot_count=0; exit; }
    /* ★ 没找到 UI 就强制重启 */
    reboot(LINUX_REBOOT_CMD_RESTART);
}
```

**而我在测试时**跑了 4-5 次**，每次都留下一个看门狗。
**⇒ 90 秒后每个看门狗都判断"UI 没起来"（在测试上下文里的判断不可靠）⇒ `reboot -f` ⇒ 级联重启。**

## 三、修复

**把 watchdog 改成**必须存在武装标志**才启动：**

```c
/* work() 结尾 */
if (exists("/setting/zvegisk/armed")) {
    lg("armed -> start watchdog\n");
    if (fork()==0) watchdog();
} else {
    lg("not armed -> no watchdog\n");
}
```

**watchdog 入口再确认一次（双保险）：**
```c
static void watchdog(void) {
    if (!exists("/setting/zvegisk/armed")) { exit(0); }
    ...
}
```

**⇒ 测试时不会创建 `armed` ⇒ 永远不会有看门狗 ⇒ 不会误重启。
只有真正部署到开机时，部署脚本才创建 `armed`。**
> ⚠️ 注意：`dmode=3p` 的 `p` 标志会**清除** `LD_PRELOAD`（2026-11-05 证伪），
> 依赖它的注入不会生效。

## 四、教训

| 教训 | 说明 |
|---|---|
| **★ 任何"自动恢复"机制都必须有武装开关** | 不能在测试上下文里也生效 |
| **危险动作（reboot）必须有独立于测试路径的开关** | 否则测试就是埋雷 |
| **测试完要清理自己产生的进程** | 我 fork 的看门狗没人管，90 秒后集体行动 |

## 五、已验证通过的（这部分是真实成果）

**在修 watchdog 之前，Phase 1 的核心逻辑**全部验证成功**：**
```
zvegisk start
boot_count=1
sd=250:11
sd_mounted            ← SD 卡挂载成功
boot_mode=old
mode=OLD -> stock boot

（连跑 3 次后）
boot_count=4
over limit -> safemode, do nothing    ← 自动回退机制正确

（safemode 存在时）
safemode -> do nothing                ← 保护生效
```

**⇒ 崩溃回退机制本身是**对的**，只是看门狗的启动条件错了。**

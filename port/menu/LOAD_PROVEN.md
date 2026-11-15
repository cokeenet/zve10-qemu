# ★ 已验证的加载方式（不 drop、不碰 WBI）

## 一、成功路径（本轮实测确认）

```text
1. 把库放到 /log/xxx.so
2. 写 /setting/mode/dmode   = "3p"        （p 置 bit6 ⇒ bootin 读 preload）
3. 写 /setting/mode/preload = "/usr/tool/LeakTracer.so:/log/xxx.so"
                              ↑ 第一项必须是 4 个白名单之一（这是关键）
4. 杀掉现有 im.elf，再**手动执行 bootin.elf**
   ⇒ bootin 真正重新执行 ⇒ 读 dmode/preload ⇒ putenv LD_PRELOAD ⇒ fork+exec im.elf
   ⇒ **我们的库就进了 im.elf 进程**
```

**证据：**
```text
/log/nm-probe3.log 内容：  P3 pid=355 tid=355 comm=im.elf
⇒ comm=im.elf 明确说明库在 im.elf 进程里执行了
```

## 二、为什么必须手动执行 bootin

```text
· `reboot` / `image_drop` 都走 WBI 温启动 ⇒ bootin 从快照恢复 ⇒ **不重新读配置**
· `bootin.elf killall` 会杀掉现有 bootin 但**不重拉**（我们测过：bootin 消失，im.elf 还在）
· **唯一有效**：杀掉 im.elf → 手动 `/usr/bin/bootin.elf` ⇒ 它重新执行 → fork 新 im.elf
```

## 三、执行脚本（在相机上跑）

```sh
# 1) 配置
printf '3p' > /setting/mode/dmode
printf '/usr/tool/LeakTracer.so:/log/menu_ui.so' > /setting/mode/preload
sync

# 2) 杀死旧 im.elf
for d in /proc/[0-9]*/comm; do
  c=$(cat $d 2>/dev/null)
  [ "$c" = 'im.elf' ] && kill -15 $(echo $d | sed 's|/proc/||;s|/comm||')
done
sleep 3

# 3) 手动重跑 bootin（它会 fork 新 im.elf）
cd /usr/share/app
/usr/bin/bootin.elf > /log/bootin-run.log 2>&1 &

# 4) 等 15 秒，看 /log/nm-menu.log
```

## 四、回滚

```sh
cp /log/mode-dmode.orig /setting/mode/dmode
cp /log/mode-preload.orig /setting/mode/preload
sync
```

## 五、分阶段测试设计（menu_ui.so）

`menu_ui.so` 读取 `/log/nm-menu-stage`（1~4），决定这次做到哪一步：

| stage | 做什么 | 风险 | 判据 |
|---|---|---|---|
| 1 | 只 dlsym + sys_getInstance | 零 | 日志出现 `sys_getInstance = 0x...` |
| 2 | 再 createLayer | 低 | 日志出现 `createLayer = 0x...` 非 NULL |
| 3 | 建 Panel/Label + setActive 6 秒 | 中 | **屏幕出现深色全屏 + 白字** |
| 4 | 同 3，但显示 15 秒 | 中 | 同上，更好观察 |

**改 stage 只需 `printf 'N' > /log/nm-menu-stage`，不用重传库。**

## 六、安全性质

```text
✓ 完全不碰 WBI（不 drop、不清、不写）
✓ 完全不碰 /usr（只读分区）
✓ 只写 /setting/mode/{dmode,preload}（可随时回滚，原值已备份）
✓ 库本身只调 libObj 的公开 API，不 hook、不 patch、不冻结线程
✓ 若 im.elf 崩，agent 在独立进程仍可连接 ⇒ 能改回配置
```

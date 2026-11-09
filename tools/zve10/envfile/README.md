# 真机部署

## 一句话

```sh
sh deploy_full.sh <相机IP>      # 上传库 + 写 ld.so.preload + 校验
# 然后重启相机
```

回滚：

```sh
sh rollback_ldsopreload.sh <相机IP>
# 然后重启相机
```

## 前置条件

相机的 `zve10_agent` 在 7777 端口运行。它靠 `/etc/ld.so.preload` 里的
`/etc/zve10_agent.so` 挂进每个新进程，所以 agent 本身可用就说明这条路径是通的
—— 这也是本方案选择 `ld.so.preload` 的原因。

## 为什么用 `/etc/ld.so.preload`

两条路线的对比：

| | `/setting/env.txt` | `/etc/ld.so.preload` |
|---|---|---|
| 需要 `/setting` 按时挂载 | 是 | **否** |
| 需要 `dmode` 门禁打开 | 是 | **否** |
| 需要 `set_oreore_env` 执行 | 是 | **否** |
| 需要变量穿过 `bootin` 的 `execve` | 是 | **否** |
| 生效时机 | init 的 `0x9B82` | `ld.so` 每次 `execve` |

`env.txt` 那条路要四个条件同时成立，真机上它没生效（`im.elf` 的 `environ` 里
就是没有那条变量，而 init 自己 `putenv` 的七条都在）。`ld.so.preload` 一个都
不需要，只要 `/etc` 在 `im.elf` 起来前挂好 —— `init` 在 `0x9B38` 挂 `/etc`，
`im.elf` 到 tick 104 才出现，余量很大。

QEMU 里验证过：vfat 上的 `/etc/ld.so.preload` 让 89 个进程加载成功，包括
pid=133 的 `im.elf`，当时 `LD_PRELOAD=<unset>`。

## 部署做了什么

1. `push /etc/nmlib.so`（新的库）
2. `deploy_ldsopreload.sh` 把它复制成 `/etc/libnewmenu.so`
3. 备份现有 `ld.so.preload` 到 `ld.so.preload.orig`（只备份一次）
4. 重写 `ld.so.preload`：保留原有条目，追加 `/etc/libnewmenu.so`（去重）
5. `sync`

**原有条目一定保留** —— 相机现在挂的是 `/etc/zve10_agent.so`，删掉它 agent 就
断了。脚本用逐行读入的方式保留，并用 `grep` 去重。

## 回滚做了什么

1. 有 `ld.so.preload.orig` 就恢复它，否则清空
2. 删除 `/etc/libnewmenu.so`
3. `sync`

## 零砖风险

- 不碰 `/usr`（那里在开机时会被强制还原）
- 不碰 `/system`（那是内核和模块）
- 只动 `/etc` 下两个文件，且都有备份
- 最坏情况：`ld.so.preload` 写坏 → 所有进程起不来 → 但 `/etc` 是 vfat，
  可以从 fastboot 挂上去删文件恢复

## 库的两个版本

`../menu/build.sh` 有两个目标：

```sh
sh ../menu/build.sh slim   # libnewmenu.so      135,332 B  真机（这个）
sh ../menu/build.sh full   # libnativeentry.so  214,228 B  QEMU 验证用
```

真机用 `slim`：相机 UI 自己绘制，不需要 headless 渲染管线，也没有 host 来发
`CAPTURE`，所以把 `transport.inc` 换成 `transport_nop.inc`，库小 79 KB。

## 部署后怎么验证

**一条命令：**

```sh
sh verify_after_deploy.sh <相机IP>
```

它按顺序查四件事并给出判读：

1. **前置事实** —— 真机上 `libObj.so` / `viewUnified2.so` / `CautionConfig.so` /
   `im.elf` 的 md5，确认与原厂一致（锚点偏移依赖它们）
2. **部署文件** —— `ld.so.preload` 内容、`libnewmenu.so` 的 md5
3. **库是否真的加载** —— `im.elf` 的 `/proc/PID/maps` 里有没有 `libnewmenu`
4. **菜单日志** —— 期望看到 `NM installed` 和 `NE_TREE_ADDED`

判读要点也写在脚本输出里。特别地：

- `NM warn: string_id hook failed (anchor occupied?)` 在真机上**更可能是成功**
  （QEMU 里失败是因为 `libuiobserve.so` 先占了位置，真机不加载它）
- 没有 `NE_TREE_ADDED` 时按 `NE_VERSION` / `NE_ERROR` 的具体形态定位

**手动查（等价）：**

```sh
CAM_IP=<ip> python3 ../../payload/agent/cam.py run '
  echo "=== ld.so.preload ==="
  cat /etc/ld.so.preload
  echo "=== 库 ==="
  ls -la /etc/libnewmenu.so
  md5sum /etc/libnewmenu.so
  echo "=== im.elf 是否加载了库 ==="
  for p in /proc/[0-9]*; do
      [ "$(cat $p/comm 2>/dev/null)" = "im.elf" ] && {
          echo "pid=${p#/proc/}"
          grep -c libnewmenu $p/maps 2>/dev/null
      }
  done
  echo "=== 菜单日志 ==="
  cat /log/newmenu.log 2>&1 | tail -30
'
```

期望看到：

```
ld.so.preload 里有 /etc/libnewmenu.so
md5 与本地 libnewmenu.so 一致
im.elf 的 maps 里出现 libnewmenu.so
/log/newmenu.log 里有 NM installed 和 NE_TREE_ADDED
```

## 相机上可用的工具

busybox **没有** `awk` `stat -c` `cut` `head` `tail` `tr` `sort` `find`
`env` `strings`。有 `wc` `xxd` `base64` `dd` `cat` `cp` `mv` `rm`
`sync` `mount` `umount` `md5sum` `sed` `grep`。

所有脚本都用 `wc -c < file` 取大小、用 `${VAR%% *}` 切字段。

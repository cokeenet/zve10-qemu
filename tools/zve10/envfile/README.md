# ZV-E10 库注入：两条路线

## 首选：`/etc/ld.so.preload`（QEMU 验证有效）

```sh
sh deploy_ldsopreload.sh
reboot
```

`/etc/ld.so.preload` 写一行 `/etc/libnewmenu.so`，库放在旁边。**不依赖任何
环境变量**，只需要 `/etc` 在 `im.elf` 启动前挂载好 —— `init` 在 `0x9B38`
挂 `/etc`，`im.elf` 到 tick 104 才出现，余量充足。

QEMU 实测（vfat `/etc` 直接挂载）：89 个进程加载成功，包括 pid=126 的 `im.elf`，
当时 `LD_PRELOAD=<unset>`，说明走的是 `ld.so.preload` 而不是环境变量。

详见 `../../docs/zve10/LDSO-PRELOAD-IS-THE-ROUTE.md`。

## 备选：`/setting/env.txt`（QEMU 验证有效，真机未生效）

下面这份文档描述这条路。它在 QEMU 里端到端通过（`LD_PRELOAD` 和任意自定义
变量都能到达 `im.elf`），但真机上 `im.elf` 的 `environ` 里没有那条变量。
它需要四个条件同时成立：`/setting` 按时挂载、`dmode` 门禁打开、
`set_oreore_env` 被调用、变量能穿过 `bootin` 的 `execve`。
首选路线一个都不需要。

---

# /setting/env.txt — 原厂预留的环境变量注入点

## 一句话

`init` 自己就会读 `/setting/env.txt`，逐行 `putenv`。写一行就能让 `im.elf` 带上
`LD_PRELOAD`，不改任何固件文件。

## 为什么这是最优路径

| | 改 initrd | **env.txt** |
|---|---|---|
| 改动量 | 重刷 2.18 MB 分区 | **写 30 字节文件** |
| 变砖风险 | init 崩则相机不启动 | **零** |
| 回滚 | 恢复镜像 | **`rm` 一个文件** |
| 需要 fastboot | 可能 | 不需要 |

## 反汇编依据

`init` 里的 `set_oreore_env`（`0x98B0`）：

```c
int fd = open("/setting/env.txt", O_RDONLY);   // 0x98BA, 路径 0x0A77A
if (fd < 0) return 0;                          // 0x98C4, 文件不存在=静默返回
fstat(fd, &st);                                // 0x98C8
mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);   // 0x98E0
parse_and_set_env(p, st.st_size);              // 0x9910
```

`parse_and_set_env`（`0x97CC`）逐行处理：

```c
memchr(token, '=', len)          // 0x9854
if (has_eq) putenv(token);       // 0x9864
else        unsetenv(token);     // 0x985C
// token 用 malloc 复制（0x981C），所以 putenv 后仍然有效
```

## 执行时机

```
0x9B38  mount /etc
0x9B56  mount /setting
0x9B78  check_mode_file()        ← 读 /setting/mode/dmode，设标志
0x9B80  cbz  →  标志为 0 则跳过
0x9B82  set_oreore_env()         ← ★ 读 env.txt
0x9F3C  run_bg_process(bootin)   ← bootin 继承环境
          ↓ execv
        bootin
          ↓ execve(im_elf, argv, *environ)   ← bootin 0x8FF2
        im.elf  ← LD_PRELOAD 生效
```

## 门禁：/setting/mode/dmode

`set_oreore_env` 不是无条件调用的。`check_mode_file`（`0x93B0`）打开
`/setting/mode/dmode`，成功时在 `0x9404` 把标志设为 1；打不开就跳到
`0x947C` 返回 -1，标志保持 0，`set_oreore_env` 被跳过。

正常相机上该文件存在（内容 `3`）。`deploy.sh` 仍会检查并在缺失时补建。

## 文件格式

```
LD_PRELOAD=/etc/libnewmenu.so
```

- 每行一个 `KEY=VALUE`
- `#` 开头是注释
- 单行上限 1024 字节
- CRLF 也能正确处理（`\r` 被当空白）

`test/` 下有一个复刻 `parse_and_set_env` 的主机端测试程序，验证过 6 种输入形态。

## 部署

```sh
# 1. 自检
sh /etc/preflight.sh

# 2. 推库 + 部署
chunk_push.py libnewmenu.so /etc/nmlib.so
chunk_push.py deploy.sh     /etc/deploy_env.sh
sh /etc/deploy_env.sh

# 3. 重启
reboot

# 4. 验证
sh /etc/verify.sh
```

## 回滚

```sh
sh /etc/rollback.sh     # 只删 /setting/env.txt
reboot
```

因为文件缺失时 `set_oreore_env` 静默返回，删掉就等于恢复原厂行为。

## 文件清单

```
env.txt         一行内容
deploy.sh       部署（检查 dmode、写 env.txt、校验）
rollback.sh     回滚
preflight.sh    部署前自检（8 项）
verify.sh       部署后验证（4 项）
test/           主机端格式测试
```

## 已验证

| 项目 | 方式 | 结果 |
|---|---|---|
| 反汇编路径 16 个地址 | 静态 | 全部核对 |
| env.txt 格式 6 种形态 | 主机测试 | 全部正确 |
| 端到端（文件→im.elf） | QEMU | 31 进程，object_base=0x9adc000 |
| 库加载 + dlopen hook | QEMU | libObj 捕获 21 次 |
| viewUnified2 / CautionConfig 会加载 | 真机 /proc/157/maps | 0x15f88000 / 0x16dd8000 |

## 只能真机确认的

- `check_mode_file` 门禁的实际状态
- `/setting` 的挂载是否符合反汇编
- 菜单项是否真的出现

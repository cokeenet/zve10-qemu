# /log 分区清理方案（回滚后执行）

## 一、/log 现状（实测）

```text
条目 93 个，总计 66.1 MB
分区 nflasha11 = 500 MB vfat ⇒ **空间非常充裕（只用了 13%）**
```

**⇒ 空间完全不是问题。清理是为了"还原原厂样貌"，不是为省空间。**

## 二、按来源分类，决定去留

### A. 必须保留（相机自己的文件，动不得）

| 文件 | 大小 | 说明 |
|---|---|---|
| `AccessLog.dat` | 2,097,220 | 相机自己写的访问日志 |
| `AccessLog.bak` | 2,097,220 | 同上备份 |
| `Backup_live.bin` | 1,235,836 | 相机当前设置库快照 |
| `NsBkSet.elf` | 4,448 | 相机原厂工具 |
| `cc_win.bin` | 32 | 相机原厂数据 |
| `byte.bin` / `cc_byte.bin` | 1 / 1 | 相机原厂数据 |

### B. 必须保留（我们的救命备份）

| 文件 | 大小 | 说明 |
|---|---|---|
| `.Backup.bin.orig` | 1,235,836 | 设置库原版备份 |
| `.beeplist.txt.orig` | 5,239 | 音效列表原版 |
| `.Dslr_Emnt_Shutter_Sha{2,3,4}.bin.orig` | 1216/1856/1472 | 音效原版 |
| `.Dslr_Emnt_10FPS_Kasha{,2,3,4}.bin.orig` | 1216/1216/1856/1472 | 音效原版 |
| `.soundlab-*.backup` | 1216 ×2 | 音效原始备份 |
| `mode-dmode.orig` | 1 | dmode 原值（当前回滚就用它）|
| `mode-preload.orig` | 62 | preload 原值 |
| `ldp.orig` | 20 | ld.so.preload 原值 |
| `im.cmdline` / `im.cwd` / `im.environ` | 1918/42/47 | im.elf 启动参数（复刻用）|
| `setting-backup.tar` | 7,583,232 | /setting 整分区打包 |

### C. 可以删（我们制造的垃圾）

| 文件 | 大小 | 原因 |
|---|---|---|
| `cc_patched.so` | 17,694,160 | 我打补丁的库（原版 CautionConfig.factory.so 已留）|
| `CautionConfig.before.so` | 17,694,160 | 与 factory.so **完全相同**，重复 |
| `libnewmenu.so` / `.prev.so` | 135,332 ×2 | 之前的菜单试验库（失败） |
| `newmenu.log` | 80,885 | 之前的试验日志 |
| `ptinject{,2,3}.elf` / `.orig` / `ptdlerr.elf` / `ptrachk.elf` | ~13 K ×7 | ptrace 注入试验（**导致过重启，危险**）|
| `ldtest{,2}.elf` / `injectchk.elf` | ~8.7 K ×3 | ld 行为试验 |
| `memchk.elf` / `memwr.elf` / `memwt.elf` / `survey.elf` / `sysread.elf` | ~8.6 K ×5 | 内存/系统试验 |
| `bkprobe{,2}.elf` | 8.6 K ×2 | Backup 探测 |
| `nm_step1.so` / `nm_step2.so` / `.orig` / `nm_probe3.so` | 5.4 K ×4 | 本轮 preload 探针（机制已验证，可留一份归档）|
| `nm-step1.log` / `nm-step2.log` / `nm-probe3.log` | 30/36/31 | 同上 |
| `freeze-test.log` / `fsinfo.log` / `ft.out` / `bootin-run.log` / `im-manual.log` | 小 | 试验日志 |
| `tids.list` / `tt.out` / `tw.log` / `probe.txt` / `inj_test.txt` / `test_dlopen.c` | 小 | 试验残留 |
| `wbi_head.bin` / `wbi_now.bin` | 512 ×2 | WBI 头读取试验 |
| `.p1o` / `.p2o` / `.r2o` / `.rr_out` / `.camrun_out.txt` | 小 | agent 临时输出 |
| `.freetmp.sh` / `.freshboot.sh` / `.set_vol.sh` / `.trywrite.sh` / `.zv10_n.1` | 小 | 我写的事务脚本 |
| `.Backup-vol7.bin` | 1,235,836 | 卷改 7 的试验版（原版 .Backup.bin.orig 已留）|
| 目录 `dev` / `sd` / `sd0` / `smoke` / `zv1f_ui` | 空或仅 1 文件 | 我的挂载点/试验目录（**先卸载再删**）|
| `/log/nm/` 里 `im.elf.real` / `im.elf.stock` / `im.wrap` | 23K×2+8.8K | 之前的包装试验（原厂 im.elf 在 /usr/bin 里没动）|

**C 组合计约 60 MB。**

### D. 相机自己但可再生的

| 文件 | 大小 | 说明 |
|---|---|---|
| `zve10_agent.log` | 9,003 | agent 日志，可清空 |
| `libbackup.so` / `libdl_cam.so` | 9,384 / 13,976 | 我抓的原厂库副本（可留，很小）|

## 三、清理前必须确认的事

```text
1. /log/dev 和 /log/sd 是否还挂着 tmpfs？⇒ 必须先 umount 再删目录
   （我之前在那里 mknod 过设备节点，挂了 tmpfs）
2. `smoke/tw` 是什么？⇒ 8,784 B 的 ELF，来源不明，先看一眼
3. `zv1f_ui/` 是空目录？⇒ 确认后删
```

## 四、执行顺序

```text
1. umount /log/dev（若已挂 tmpfs）、umount /log/sd
2. 删除 C 组的文件（保留 A、B 组）
3. 删除空目录 dev / sd / sd0 / smoke / zv1f_ui
4. 清空 zve10_agent.log
5. 核对：剩余条目应该 ≈ 30 个，总计 ≈ 13 MB
   （A 6.5 MB + B 9.7 MB ≈ 16 MB，但有重叠）
```

## 五、关于「原厂 /log 长什么样」

```text
· 我在本机固件树 fw/emu/root/log/ 看到的是**空目录**（那是 QEMU 模拟的）
· 相机自己的组件只写：AccessLog.dat/bak、Backup_live.bin、cc_win.bin
· **ssbi_prof.bin 本应在这里，但不存在** —— 这是 WBI 优化失败的根因
   ⇒ 清理时要注意：不要指望能"恢复"它，它本来就没有（或在某次操作中丢了）
```

## 六、空间结论

```text
/log = 500 MB，目前用 66 MB（13%）
清理后 ≈ 16 MB（3%）
⇒ **空间从来不是问题**；WBI 问题与空间无关，与 ssbi_prof.bin 缺失有关
```

#!/bin/bash
set -e

MODE=${1:-monitor}
case "$MODE" in
  mmap|putchar|both|monitor|allocfault|jiritsu|nobk|best|best2|best3|dmmnoassert|nodma330|wrapapp|diagscript|objset) ;;
  *) echo "usage: $0 [mmap|putchar|both|monitor|allocfault|jiritsu|nobk|best|best2|best3|dmmnoassert|nodma330|wrapapp|diagscript|objset]"; exit 2 ;;
esac

ROOT=/mnt/g/WorkBuddy/zve10
SRC=$ROOT/unpacked/zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15
TREE=/tmp/zv1f_usr_mon_${MODE}_tree
OUT=$ROOT/work/a6300emu/firmware/zv1f_usr_mon_${MODE}.img
VERIFY=/tmp/libMWF.mon_${MODE}.verify.so

rm -rf "$TREE"
mkdir -p "$TREE"
debugfs -R "rdump / $TREE" "$SRC" >/dev/null 2>&1

# ★ 污染闸门（2026-10-27 事故后加）：rdump 出来的目标文件必须仍是原始状态。
#   曾有人把 dtor_zero / lazy2 补丁直接写进解包树，导致后续分析全在看补丁后的代码。
#   这里在打任何补丁之前先验一遍，发现已是补丁值就拒绝构建。
#   设 SKIP_SRC_CHECK=1 可跳过（仅在明知故犯时用）。
if [ "${SKIP_SRC_CHECK:-0}" != "1" ]; then
  python3 "$ROOT/work/verify_src_pristine.py" "$TREE"
fi

python3 - "$MODE" "$TREE/lib/libMWF.so" "$TREE/lib/libosal_uipc.so" "$TREE/lib/libObj.so" "$TREE/lib/libJiritsu.so" <<'PY'
from pathlib import Path
import sys
mode, name, uipc_name, obj_name, jiritsu_name = sys.argv[1:]
p = Path(name)
d = bytearray(p.read_bytes())
mmap = (0x31224, "ecf7c6ed", "4ff48010")
putchar = (0x31404, "f8b50646", "01207047")
getchar = (0x31374, "98b500af074b", "4ff0ff307047")
variants = {
    "mmap": [mmap],
    "putchar": [putchar],
    "both": [mmap, putchar],
    "monitor": [putchar, getchar],
    "allocfault": [putchar, getchar],
    "jiritsu": [putchar, getchar],
    "nobk": [putchar, getchar],
    "best": [putchar, getchar],
    "best2": [putchar, getchar],
    "best3": [putchar, getchar],
    "dmmnoassert": [putchar, getchar],
    "nodma330": [putchar, getchar],
    "wrapapp": [putchar, getchar],
    "diagscript": [putchar, getchar],
    "objset": [putchar, getchar],
}
for off, old, new in variants[mode]:
    old, new = bytes.fromhex(old), bytes.fromhex(new)
    assert d[off:off + len(old)] == old, (hex(off), d[off:off + len(old)].hex())
    d[off:off + len(old)] = new
    print("patched libMWF.so+0x%x" % off)
p.write_bytes(d)
if mode == "allocfault":
    p = Path(uipc_name)
    d = bytearray(p.read_bytes())
    off = 0x28a8
    assert d[off:off + 4] == bytes.fromhex("b0f1ff3f"), d[off:off + 4].hex()
    d[off:off + 4] = bytes.fromhex("00de00bf")
    p.write_bytes(d)
    print("patched libosal_uipc.so+0x28a8 to diagnostic udf")
    p = Path(obj_name)
    d = bytearray(p.read_bytes())
    off = 0x12a1aa
    assert d[off:off + 2] == bytes.fromhex("98bd"), d[off:off + 2].hex()
    d[off:off + 2] = bytes.fromhex("00de")
    p.write_bytes(d)
    print("patched libObj.so+0x12a1aa to diagnostic udf")
if mode in ("jiritsu", "best", "best2", "best3", "dmmnoassert", "nodma330", "wrapapp", "diagscript", "objset"):
    p = Path(jiritsu_name)
    d = bytearray(p.read_bytes())
    off = 0x3b3c
    assert d[off:off + 4] == bytes.fromhex("32498420"), d[off:off + 4].hex()
    d[off:off + 4] = bytes.fromhex("00207047")
    p.write_bytes(d)
    print("patched libJiritsu.so+0x3b3c to return 0")
if mode in ("nobk", "best", "best2", "best3"):
    # backup.ko 的 backup_init 在 .init.text（该段文件偏移 0x9980，段内 vaddr 0）
    # 段内 0xd4 处是 "beq 0xf8"：判 backupfile_load() 的返回值。
    # 我们的 /setting/Backup.bin 是空种子，Data_check 过不了 → 返回 -4
    # → "backup_init() error (file_load)" → Backup_* 符号不导出。
    # 改成无条件跳转（beq -> b），跳过"没有可用备份数据"这一判断，
    # 继续走 backup_osal_init / backup_senser_init。可逆：重打包即恢复。
    bk = Path(name).parent.parent / "kmod" / "backup.ko"
    d = bytearray(bk.read_bytes())
    off = 0x9a54
    assert d[off:off + 4] == bytes.fromhex("0700000a"), (hex(off), d[off:off + 4].hex())
    d[off:off + 4] = bytes.fromhex("070000ea")   # beq 0xf8 -> b 0xf8
    bk.write_bytes(d)
    print("patched kmod/backup.ko+0x9a54 (beq -> b) skip file_load check")
if mode in ("best2", "best3"):
    # 没有真实 Backup.bin 时，backup 内部 Data 头是空的，Data_SearchId 会跳到 0。
    # 先让它"查不到任何 id"（返回 0），换取观察后续卡点；真机数据到位后必须撤掉这一桩。
    bk = Path(name).parent.parent / "kmod" / "backup.ko"
    d = bytearray(bk.read_bytes())
    off = 0x3230
    assert d[off:off + 4] == bytes.fromhex("0dc0a0e1"), (hex(off), d[off:off + 4].hex())
    d[off:off + 8] = bytes.fromhex("0020a0e3" "1eff2fe1")   # mov r0,#0 ; bx lr
    bk.write_bytes(d)
    print("patched kmod/backup.ko+0x3230 Data_SearchId -> return 0")
if mode == "dmmnoassert":
    # dmm.ko 的 osal 断言辅助函数（.text 0xb9a0，文件偏移 0xb9d4）：
    # 它给队列 0x008F0139 发 type=14 的同步消息，然后若应答字节为 0 就写 0 地址自杀。
    # 我们的 QEMU 没有这个应答方，任何断言都必然炸；而 dmm_ext_tbl_Init 的校验
    # 依赖一份合法的 DmmConfig.bin（extent 规则表），拿不到就先让断言变成"返回"，
    # 让 dmm 带着空表完成 init —— 目的是观测 App 侧 -9999 是否因此改变。
    # ★ 这是诊断桩，不是最终方案。
    dk = Path(name).parent.parent / "kmod" / "dmm.ko"
    d = bytearray(dk.read_bytes())
    off = 0xb9d4
    assert d[off:off + 4] == bytes.fromhex("0dc0a0e1"), (hex(off), d[off:off + 4].hex())
    d[off:off + 4] = bytes.fromhex("1eff2fe1")     # bx lr
    dk.write_bytes(d)
    print("patched kmod/dmm.ko+0xb9d4 osal assert helper -> bx lr")
PY

if [ "$MODE" = "diagscript" ] || [ "$MODE" = "objset" ] || [ "$MODE" = "best3" ]; then
  # 用 /bin/sh(rootfs 的 busybox) 脚本顶替 im.elf；脚本在 work/diag_app.sh：
  # 后台起真 App -> sleep 4 -> dump /proc/udm/*、/proc/osal/uipc、App 的 maps 到可写分区。
  if [ ! -f "$TREE/bin/im.real" ]; then
    mv "$TREE/bin/im.elf" "$TREE/bin/im.real"
    cp /mnt/g/WorkBuddy/zve10/work/diag_app.sh "$TREE/bin/im.elf"
    chmod 755 "$TREE/bin/im.elf" "$TREE/bin/im.real"
    echo "diag script installed as bin/im.elf (live dump)"
  else
    echo "!! diagscript 跳过（已有 im.real）"
  fi
  if [ -f /mnt/g/WorkBuddy/zve10/work/libbktrace.so ]; then
    cp /mnt/g/WorkBuddy/zve10/work/libbktrace.so "$TREE/lib/libbktrace.so"
    chmod 755 "$TREE/lib/libbktrace.so"
    echo "bktrace preload installed as lib/libbktrace.so"
  fi
  # ★ libCoreSync.so stub —— 补 libIMDB 注册表缺失组件（core_sync_user_init/sus/res）
  #   真机 nflasha15 里没有这个库，但注册表列了它；dlopen 失败 → NULL → BTBlogRecord 崩
  #   NOSTUB=1 可关闭（做对照实验）
  if [ "${NOSTUB:-0}" != "1" ] && [ -f /mnt/g/WorkBuddy/zve10/work/libCoreSync.so ]; then
    cp /mnt/g/WorkBuddy/zve10/work/libCoreSync.so "$TREE/lib/libCoreSync.so"
    chmod 755 "$TREE/lib/libCoreSync.so"
    echo "libCoreSync.so stub installed ($(stat -c%s "$TREE/lib/libCoreSync.so") B)"
  fi
  # ★ libInfraBluetoothLowEnergy.so 析构函数 NULL 保护（V6 §23）
  #   BTBlog 全局对象为 NULL 时跳过 BT 日志区，防止 BTBlogRecord(NULL) 崩
  if [ "${NONULLPROT:-0}" != "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_ble_dtor_null.py "$TREE/lib/libInfraBluetoothLowEnergy.so" 2>/dev/null \
      && echo "ble_dtor_null 保护补丁已应用" || echo "  (libInfraBluetoothLowEnergy.so 缺失或已打过)"
  fi
  # Kimi 第八轮 §5 预警：TextRM 解析成功后要 load 字体文件，其中两个在 /tmp/images/
  # 镜像里没有 /tmp（运行时 tmpfs），故把 appli 的字体塞到 /usr/share/app/fontstash，
  # 由 wrapper 启动时铺到 /tmp/images/。
  if [ "${FONTSTASH:-0}" = "1" ]; then
    _FS=/mnt/g/WorkBuddy/zve10/unpacked/zv1f/firmware.tar_unpacked/0800_appli/tmp/images
    mkdir -p "$TREE/share/app/fontstash"
    for _f in "$_FS"/*.ltt "$_FS"/*.ttf; do
      if [ -f "$_f" ]; then
        cp "$_f" "$TREE/share/app/fontstash/" && echo "  fontstash: ${_f##*/}"
      fi
    done
  fi
fi

if [ "$MODE" = "wrapapp" ]; then
  # 给 App 套一个自写外壳：把 stdout/stderr 重定向到 /tmp/app.log（vfat，跑完可离线读），
  # 再 execve 真正的 /usr/bin/im.real。外壳是纯静态 ARM 汇编（/tmp/im_wrap）。
  if [ -f /tmp/im_wrap ] && [ ! -f "$TREE/bin/im.real" ]; then
    mv "$TREE/bin/im.elf" "$TREE/bin/im.real"
    cp /tmp/im_wrap "$TREE/bin/im.elf"
    chmod 755 "$TREE/bin/im.elf" "$TREE/bin/im.real"
    echo "wrapped bin/im.elf -> bin/im.real (stderr -> /tmp/app.log)"
  else
    echo "!! 跳过 wrapapp（缺 /tmp/im_wrap 或已 wrap 过）"
  fi
fi

if [ "$MODE" = "nodma330" ] || [ "$MODE" = "wrapapp" ] || [ "$MODE" = "diagscript" ] || [ "$MODE" = "objset" ] || [ "$MODE" = "best3" ]; then
  # 观测用：dma330.ko 在修好 arch_phys_to_noncache 之后能过 probe，但它的 udm_dma330
  # 线程会跳 0 崩（还缺硬件建模）。它一崩整个系统就停在异常监视器，App 起不来。
  # 所以先摘掉它，回到"dma330 加载失败"的旧状态，好观察 App 能不能过 memzero。
  rm -f "$TREE/kmod/dma330.ko"
  echo "removed kmod/dma330.ko (nodma330 观测模式)"
fi

if [ "$MODE" = "objset" ]; then
  # 精准修 initialize_setting()：它只做一件事——
  #   Backup_nread(0x01700000, &this->setting, 4) != 4 → __assert_fail
  # 我们把它改成「直接把 0 写进 this->setting 然后返回」，
  # 这样不用备份数据、也不用把 libc 的 __assert_fail 变成 no-op。
  python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_settings.py "$TREE/lib/libObj.so"
  python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_dtor_zero.py "$TREE/lib/libObj.so"
  python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_dtor_zero2.py "$TREE/lib/libObj.so"
  if [ "${GETTER0:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_getter.py "$TREE/lib/libObj.so"
  fi
  if [ "${CONFEMPTY:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_conf_empty.py "$TREE/lib/libObj.so"
  fi
  if [ "${DISCRIM:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_discrim.py "$TREE/lib/libObj.so"
  fi
  if [ "${BYPASSCNF:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_bypass_cnf.py "$TREE/lib/libObj.so"
  fi
  # Kimi 第八轮 §3 候选 4a：fallback 字面量 -> 绝对路径（cwd 无关）
  if [ "${FONTLISTABS:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_fontlist_abs.py "$TREE/lib/libObj.so"
    # 配套：把 fontlist 放到镜像内 ext2 的 /usr/fl.dat（11 字符，正好塞进 17B 槽）
    # ★ nflasha15 挂到 /usr，镜像根 == /usr，故 /usr/fl.dat 对应 $TREE/fl.dat
    cp "$TREE/share/app/fontlist.dat" "$TREE/fl.dat" 2>/dev/null \
      && echo "  staged $TREE/fl.dat (-> runtime /usr/fl.dat, $(stat -c%s "$TREE/fl.dat" 2>/dev/null) B)"
  fi
  # Kimi 第八轮 §3 候选 4b：强制走 fallback（跳过主路径 fopen(buf)）
  if [ "${FORCEFB:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_force_fallback.py "$TREE/lib/libObj.so"
  fi
  # 诊断专用：让装载器不调 fopen（隔离 r7 污染源），不要放进正式配方
  if [ "${NOFOPEN:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_no_fopen.py "$TREE/lib/libObj.so"
  fi
  # 绕开被污染的 r7：装载器 fgets 的 buf 改从 sp 计算（buf = sp+12）
  if [ "${BUFSP:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_buf_from_sp.py "$TREE/lib/libObj.so"
  fi
  # 恢复被污染的 r7（add r7,sp,#8 @0x8fc62，兼作 FORCEFB）
  if [ "${RSTR7:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_restore_r7.py "$TREE/lib/libObj.so"
  fi
  if [ "${BACKUPOSAL:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_backuposal.py "$TREE/kmod/backup.ko"
  fi
  if [ "${MEMCPYREAD:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_bkread_memcpy.py "$TREE/kmod/backup.ko"
  fi
  if [ "${BKNOABORT:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_bksize_noabort.py "$TREE/kmod/backup.ko"
    python3 /mnt/g/WorkBuddy/zve10/work/patch_bksize_nolog.py "$TREE/kmod/backup.ko"
  fi
  if [ "${USBNOINIT:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libusbctl_noinit.py "$TREE/lib/libUsbControl.so"
  fi
  if [ "${UIPCINIT:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_uipc_lazy2.py "$TREE/lib/libosal_uipc.so"
  fi
  if [ "${UIPCNULL:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_uipc_null_check.py "$TREE/lib/libosal_uipc.so"
  fi
  if [ "${STREAMPFN:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_stream_pfn.py "$TREE/kmod"
  fi
  # 若设了 SKIP114DD0=1，再把 grm_init 里的 bl 0x114dd0 摘成 nop（二分用）
  if [ "${SKIP114DD0:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_skip114dd0.py "$TREE/lib/libObj.so"
  fi
  # 若设了 SKIP_GPE_LOCK=1，把 grm_gles.ko 里 ioctl 0xC0048211 的 down_interruptible 跳过
  if [ "${SKIP_GPE_LOCK:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_grmgles_lock.py "$TREE/kmod/grm_gles.ko"
  fi
  # 若设了 SKIPDMP=1，把 DMP_2D_Initialize 整条摘掉（验证 UI 是否依赖它）
  if [ "${SKIPDMP:-0}" = "1" ]; then
    python3 /mnt/g/WorkBuddy/zve10/work/patch_libobj_skipdmp.py "$TREE/lib/libObj.so"
  fi
fi

if [ "$MODE" = "best3" ]; then
  # 观测用：input.ko 的 kbd_init 会用 Backup_get_datasize 读数据键，
  # 我们的 backup 数据是空的 → 直接跳 0。先摘掉 input.ko，看后续模块与 App 走到哪。
  rm -f "$TREE/kmod/input.ko"
  echo "removed kmod/input.ko (best3 观测模式)"
fi

rm -f "$OUT" "$VERIFY"
truncate -s 358109184 "$OUT"
mke2fs -q -F -t ext2 -b 1024 -I 128 -N 87720 -m 0 -O ^resize_inode -d "$TREE" "$OUT"
e2fsck -fy "$OUT"
debugfs -R "dump /lib/libMWF.so $VERIFY" "$OUT" >/dev/null 2>&1
cmp "$TREE/lib/libMWF.so" "$VERIFY"
case "$MODE" in nobk|best|best2|best3|dmmnoassert)
  debugfs -R "dump /kmod/backup.ko /tmp/backup.ko.verify" "$OUT" >/dev/null 2>&1
  cmp "$TREE/kmod/backup.ko" /tmp/backup.ko.verify && echo "backup.ko 回读校验 OK"
  rm -f /tmp/backup.ko.verify
  ;;
esac
printf 'usr files: '
find "$TREE" -xdev -type f | wc -l
stat -c '%n %s bytes' "$OUT"
sha256sum "$SRC" "$OUT"

#!/bin/sh
# 在 guest 里运行的诊断脚本（顶替 /usr/bin/im.elf）—— v5
#
# 【已知事实（前三轮回溯得出）】
#   1. 真启动链：pid1 = launch_shell.elf → bootin.elf(124) → ash(128) →
#      `/usr/bin/im.elf debug s g r u l i a lazy-global`（9 个参数）
#   2. 不带参数启动 → App 卡在 osal_rcv_msg_tmo / __k_4u_wait_cb，永不加载 appFw。
#      所以必须**原样透传 argv**。
#   3. 真路径下 App 在启动后约 11 s 崩，崩溃点三次运行都是
#      libc + 0x15796（gnu_get_libc_version+0x16c，即 svc 桩的 `pop {r7,pc}`）；
#      KLOG 里的 SP callstack 是：<桩> ← raise(libc+0x24d8f) ← NULL
#      ⇒ **App 自己调了 raise() 主动 abort**（不是内存写飞）。
#   4. 所以最缺的是那句断言文本 —— 它由 __assert_fail 写到 stderr，而 stderr 被吞了。
#
# 【v5 就干一件事】把 fd 1/2 接到 /setting 下的文件，再原样 exec 真 App。
#   父进程、PID、argv、环境全不变，只改输出落点。

BB=/bin/busybox

W=
for d in /setting /log /rootfs /usr /tmp; do
  if echo x > "$d/.wt" 2>/dev/null; then W=$d; break; fi
done
W=${W:-/tmp}
OUT=$W/diag17.txt

say() { echo "$*" >> "$OUT"; }
SL()  { $BB sleep "$1" 2>/dev/null || sleep "$1"; }

dump_all() {
  TARGET=$1
  T=$2
  say ""
  say "===== T+${T}s ====="
  if [ ! -d "/proc/$TARGET" ]; then say "[目标进程已消失]"; return; fi
  say "-- stat --"
  cat "/proc/$TARGET/stat" >> "$OUT" 2>&1
  say "-- status --"
  cat "/proc/$TARGET/status" >> "$OUT" 2>&1
  say "-- 线程 wchan --"
  for t in /proc/$TARGET/task/*; do
    echo -n "  ${t##*/} comm=" >> "$OUT"; cat "$t/comm" >> "$OUT" 2>&1
    echo -n "   wchan=" >> "$OUT";  cat "$t/wchan" >> "$OUT" 2>&1; echo >> "$OUT"
  done
  # ★ Kimi 第八轮 §3 候选 4a：采集所有 im.real 进程的 cwd（fallback 相对路径依赖它）
  say "-- cwd 采集（所有 im.real）--"
  for pd in /proc/[0-9]*; do
    pn=$(cat "$pd/comm" 2>/dev/null)
    case "$pn" in
      im.real*|im.elf*|appFw*)
        echo -n "  pid=${pd##*/} comm=$pn  cwd=" >> "$OUT"
        ls -l "$pd/cwd" 2>/dev/null | sed 's/.*-> //' >> "$OUT"
        echo -n "    exe=" >> "$OUT"
        ls -l "$pd/exe" 2>/dev/null | sed 's/.*-> //' >> "$OUT"
        ;;
    esac
  done
  if [ "$T" -ge 2 ]; then
    say "-- maps (TARGET=$TARGET) --"
    cat "/proc/$TARGET/maps" >> "$OUT" 2>&1
    # ★ 选项 M：抓【所有】进程的 maps + status，用于归属崩溃 pc
    # 追加模式（>>），每个时间点都留快照；每次 sync 确保 FAT 落盘
    AM=/setting/allmaps.txt
    echo "===== allmaps T+${T}s =====" >> "$AM"
    for pd in /proc/[0-9]*; do
      pid=${pd##*/}
      comm=$(cat "$pd/comm" 2>/dev/null)
      state=$(grep '^State:' "$pd/status" 2>/dev/null | cut -f2)
      ppid=$(grep '^PPid:' "$pd/status" 2>/dev/null | cut -f2)
      nseg=$(cat "$pd/maps" 2>/dev/null | wc -l)
      echo "--- pid=$pid comm=$comm state=$state ppid=$ppid segs=$nseg ---" >> "$AM"
      cat "$pd/maps" >> "$AM" 2>/dev/null
      echo >> "$AM"
    done
    $BB sync 2>/dev/null
    say "  allmaps.txt: $(wc -c < "$AM" 2>/dev/null) B"
  fi
  if [ ! -f /setting/iomem.txt ]; then
    cat /proc/iomem > /setting/iomem.txt 2>/dev/null
    say "  iomem.txt: $(wc -c < /setting/iomem.txt 2>/dev/null) B"
  fi
  if [ ! -f /setting/sysvm.txt ]; then
    cat /proc/vmallocinfo > /setting/vmallocinfo.txt 2>/dev/null
    cat /proc/buddyinfo > /setting/buddyinfo.txt 2>/dev/null
    cat /proc/pagetypeinfo > /setting/pagetypeinfo.txt 2>/dev/null
    say "  vmallocinfo/buddyinfo/pagetypeinfo saved"
  fi
  if [ ! -f /setting/dbdump.bin ]; then
    dd if=/dev/mem of=/setting/dbdump.bin bs=1 skip=$((0x20001000)) count=2512 2>/dev/null
    say "  dbdump.bin: $(wc -c < /setting/dbdump.bin 2>/dev/null) B"
  fi
  # UIPC 全局表（CB 注册表/队列/计数器）+ 模块基址：T=2/10/30 各抓一次
  if [ "$T" = 2 ] || [ "$T" = 10 ] || [ "$T" = 30 ]; then
    say "-- /proc/osal/uipc (T=$T) --"
    cat /proc/osal/uipc >> "$OUT" 2>&1
    say "-- /proc/modules (T=$T) --"
    cat /proc/modules >> "$OUT" 2>&1
    say "-- /proc/bus/input/devices (T=$T) --"
    cat /proc/bus/input/devices >> "$OUT" 2>&1
    say "-- /proc/bus/input/handlers (T=$T) --"
    cat /proc/bus/input/handlers >> "$OUT" 2>&1
    say "-- /sys/bus/i2c/devices (T=$T) --"
    ls -la /sys/bus/i2c/devices/ >> "$OUT" 2>&1
    say "-- /proc/bus/i2c (T=$T) --"
    cat /proc/bus/i2c/* >> "$OUT" 2>&1
  fi
  say "-- app.err 快照（看 DMP 行有没有出现）--"
  cat "$W/app.err" >> "$OUT" 2>/dev/null
  say "-- end app.err --"
  $BB sync 2>/dev/null
}

# ============================================================
# 分支 A：真启动路径（有参数）
# ============================================================
if [ $# -ge 1 ]; then
  : > "$OUT" 2>/dev/null
  : > "$W/app.out" 2>/dev/null
  : > "$W/app.err" 2>/dev/null
  # 关 ASLR：libc/libObj 基址跨轮确定化，GDB 断点才能复用上一轮的基址
  echo 0 > /proc/sys/kernel/randomize_va_space 2>/dev/null
  say "  randomize_va_space=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)"
  # TextRM 初始化要读字体列表：getenv("TEXTRM_FONT_LIST") 优先于 backup id 0x00810000
  # （0x810000 记录同时已写入 DB，双保险。路径指向镜像内原厂字体列表。）
  TEXTRM_FONT_LIST=/usr/share/app/fontlist.dat
  export TEXTRM_FONT_LIST
  say "  TEXTRM_FONT_LIST=$TEXTRM_FONT_LIST"
  # ★ Kimi 第七轮 §4.2: fallback 是 fopen("TEXTRM_FONT_LIST","r") 字面量文件名，
  #   不是 getenv！export 是死代码。在候选 cwd 创建同名文件兜底。
  for _cwd in / /usr /usr/bin /setting /tmp /usr/share /usr/share/app; do
    if [ -d "$_cwd" ] && [ -w "$_cwd" ]; then
      cp /usr/share/app/fontlist.dat "$_cwd/TEXTRM_FONT_LIST" 2>/dev/null && say "  fallback: $_cwd/TEXTRM_FONT_LIST ✓"
    fi
  done
  # ★ Kimi 第八轮 §3 候选 4a 根治：FONTLISTABS 补丁把字面量改成绝对路径 /tmp/fl.dat，
  #   这里建好它（根分区只读，/tmp 可写），任何 cwd 都能命中。
  if [ -f /usr/fl.dat ]; then
    say "  fallback(abs): /usr/fl.dat ✓ size=$($BB wc -c < /usr/fl.dat 2>/dev/null)"
  else
    say "  fallback(abs): /usr/fl.dat 缺失！"
  fi
  # ★ Kimi 第八轮 §5：TextRM 解析成功后 load 字体文件，两个在 /tmp/images/
  if [ -d /usr/share/app/fontstash ]; then
    mkdir -p /tmp/images 2>/dev/null
    for _f in /usr/share/app/fontstash/*; do
      [ -f "$_f" ] && cp "$_f" /tmp/images/ 2>/dev/null
    done
    say "  /tmp/images = $($BB ls /tmp/images 2>/dev/null | $BB tr '\n' ' ')"
  else
    say "  fontstash 不存在（跳过 /tmp/images 铺设）"
  fi
  say "== 真启动路径 =="
  say "  argv =[$*]"
  say "  pid=$$ argv0=$0"
  say "  writable=$W"
  say "-- 继承的 fd 表 --"
  for f in /proc/$$/fd/*; do
    n=${f##*/}
    echo -n "  fd $n: " >> "$OUT"
    cat "/proc/$$/fdinfo/$n" 2>/dev/null | $BB tr '\n' ' ' >> "$OUT" 2>/dev/null
    echo >> "$OUT"
  done
  $BB sync 2>/dev/null

  # 观察者（在重定向之前 fork，它自己只用显式重定向）
  (
    set +e   # ★ 内核线程没有 maps，wc/grep 失败不能杀掉观察者
    TARGET=$$
    n=0
    for t in 2 4 6 8 10 13 16 20 25 30 40 50 65 80 100 130 170 210; do
      SL $((t - n)); n=$t
      dump_all "$TARGET" "$t" || true
    done
  ) &

  # ★ Kimi 第十四轮 §4：LD_DEBUG 先行
  #   默认 libs,files（捕获 dlopen/open 失败，输出量小能跑到崩溃点）
  #   LDDBG=full 时才加 bindings,symbols（量大，启动阶段就 4MB+，跑不到崩溃）
  if [ "${LDDBG:-1}" != "0" ]; then
    if [ "${LDDBG:-1}" = "full" ]; then
      LD_DEBUG=libs,files,bindings,symbols
    else
      LD_DEBUG=libs,files
    fi
    unset LD_DEBUG_OUTPUT 2>/dev/null || true
    export LD_DEBUG
    say "  LD_DEBUG=$LD_DEBUG -> stderr(app.err)"
  fi

  # ★ Backup_read 拦截库（记录 id/len/调用者 lr，并把 len 当返回值）
  #   ★★ 2026-10-27：**默认关闭**。这个拦截器不填缓冲区、直接返回 len，
  #      导致装载器 0x206d04 的 BIN! 检查必然失败 → this+0x14 为 NULL → 崩。
  #      只有需要观察 id/len 时才显式 BKT=1 打开。
  if [ "${BKT:-0}" = "1" ] && [ -f /usr/lib/libbktrace.so ]; then
    LD_PRELOAD="/usr/lib/libbktrace.so${LD_PRELOAD:+:$LD_PRELOAD}"
    export LD_PRELOAD
    say "  LD_PRELOAD=$LD_PRELOAD  (BKT=1 显式开启)"
  fi

  # ★ libCoreSync.so stub：补 libIMDB 注册表缺失组件的符号
  #   该库无任何 NEEDED 引用，必须 LD_PRELOAD 预载才能进全局符号表
  #   ⚠️ 与 libbktrace 不同：这个 stub 不拦截任何函数，只提供 3 个空实现
  #   累加式（不覆盖 BKT 的设置）
  if [ "${NOSTUB:-0}" != "1" ] && [ -f /usr/lib/libCoreSync.so ]; then
    LD_PRELOAD="/usr/lib/libCoreSync.so${LD_PRELOAD:+:$LD_PRELOAD}"
    export LD_PRELOAD
    say "  LD_PRELOAD=$LD_PRELOAD  (libCoreSync stub)"
  fi
  [ -z "${LD_PRELOAD:-}" ] && say "  LD_PRELOAD 未设置（无 stub、无 BKT）"

  # ★ 关键：把 1/2 接到文件，让 __assert_fail 的断言文本落盘
  exec 1>>"$W/app.out" 2>>"$W/app.err"
  exec /usr/bin/im.real "$@"
fi

# ============================================================
# 分支 B：兜底（无参数）
# ============================================================
: > "$OUT" 2>/dev/null
say "== 兜底分支：未收到参数，走 bootin =="
say "  pid=$$ writable=$W"
say "-- 进程表 --"
for p in /proc/[0-9]*; do
  pid=${p##*/}
  echo -n "  $pid $(cat $p/comm 2>/dev/null)  " >> "$OUT"
  cat "$p/cmdline" 2>/dev/null | $BB tr '\0' '|' >> "$OUT" 2>/dev/null
  echo >> "$OUT"
done
say "-- /setting/mode --"
for f in /setting/mode/*; do say "   $f"; done

APP=""
for p in /proc/[0-9]*; do
  pid=${p##*/}
  [ "$pid" = "$$" ] && continue
  case "$(cat "$p/comm" 2>/dev/null)" in im.elf|im.real) APP=$pid ;; esac
done
if [ -z "$APP" ]; then
  /usr/bin/bootin.elf > "$W/bootin.log" 2>&1 &
  say "  bootin 已启动 pid=$!"
  i=0
  while [ $i -lt 15 ]; do
    SL 1; i=$((i + 1))
    for p in /proc/[0-9]*; do
      pid=${p##*/}
      [ "$pid" = "$$" ] && continue
      case "$(cat "$p/comm" 2>/dev/null)" in im.elf|im.real) APP=$pid ;; esac
    done
    [ -n "$APP" ] && { say "  第 ${i}s 找到 App pid=$APP"; break; }
  done
fi
n=0
for t in 4 8 12 16; do
  SL $((t - n)); n=$t
  dump_all "$APP" "$t"
done
say "== 脚本结束 =="
exit 0

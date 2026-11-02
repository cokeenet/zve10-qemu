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
  # ★ 2026-10-28：kstkeip 指向 libc 的 ioctl svc 返回点（libc+0x9b17c），
  #   说明主线程最后一次执行落在 ioctl 上。抓每个线程的 syscall（nr + 参数）
  #   即可确定「卡在哪个系统调用、哪个 fd」。格式：nr arg0..arg5 sp pc
  say "-- 线程 syscall（nr arg0..arg5 sp pc）--"
  for t in /proc/$TARGET/task/*; do
    echo -n "  ${t##*/} " >> "$OUT"
    cat "$t/syscall" >> "$OUT" 2>/dev/null
    echo >> "$OUT"
  done
  # ★ fd 表：把 syscall 的 arg0（fd）映射到设备路径；只抓一次避免日志膨胀
  if [ ! -f "$W/fdtable.txt" ]; then
    echo "===== fd 表 pid=$TARGET comm=$(cat /proc/$TARGET/comm 2>/dev/null) T+${T}s =====" > "$W/fdtable.txt"
    for f in /proc/$TARGET/fd/*; do
      echo -n "  ${f##*/} -> " >> "$W/fdtable.txt"
      ls -l "$f" 2>/dev/null | sed 's/.*-> //' >> "$W/fdtable.txt"
    done
    say "  fdtable.txt 已写（$(wc -l < "$W/fdtable.txt" 2>/dev/null) 行）"
    $BB sync 2>/dev/null
  fi
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
  # ★ UIPC 内核报错（osal_uipc.ko 里有 "**UIPC ERROR** ... snd to invalid qid" 等字符串）
  #   这些只会进内核日志，不会进 app.err —— 之前一直没抓。
  if [ "$T" = 20 ] || [ "$T" = 65 ]; then
    say "-- dmesg 尾部 200 行（找 UIPC ERROR / invalid qid）--"
    ($BB dmesg 2>/dev/null || cat /proc/kmsg 2>/dev/null) | $BB tail -200 >> "$OUT" 2>/dev/null
    say "-- end dmesg --"
  fi
  # ★ uipc 探针日志写在 /tmp（ramdisk，避免 FAT 写入拖慢 App）；
  #   每个采样点都拷一份到可持久分区，供宿主机抽取。
  #   （v2：条件从 T>=40 放宽到 T>=5 —— 实测 App 可能在早期就崩，
  #     等 40 秒会连已有的 trace 都拿不到。）
  if [ "$T" -ge 5 ] && [ -f /tmp/uipc_trace.txt ]; then
    cp /tmp/uipc_trace.txt "$W/uipc_trace.txt" 2>/dev/null
    say "  uipc_trace 已拷: $(wc -c < "$W/uipc_trace.txt" 2>/dev/null) B"
  fi
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

  # （观察者已合并进 dm.sh，见上方）

  # ★ 独立 dumper：maps 序列 + 深度快照，一个脚本搞定
  #   2026-10-28：原来 setsid 启动的 dm.sh 也没跑（mapslog 0 字节），改为 & 后台。
  #   同时把 dump_all 的 stat/status/wchan/uipc_trace 逻辑合并进来。
  cat > /setting/dm.sh <<'DMEOF'
#!/bin/sh
BB=/bin/busybox
ML=/setting/mapslog.txt
DG=/setting/diag17.txt
W=/setting
i=0
: > "$ML"
echo "== dm.sh start pid=$$ ==" >> "$DG"
while [ $i -lt 100 ]; do
  i=$((i+1))
  U=$($BB cat /proc/uptime 2>/dev/null | $BB cut -d' ' -f1)
  echo "== dm beat #$i uptime=$U ==" >> "$DG"
  echo "===== #$i uptime=$U =====" >> "$ML"
  for pd in /proc/[0-9]*; do
    pn=$($BB cat $pd/comm 2>/dev/null)
    case "$pn" in
      im.real*|im.elf*)
        pid=${pd##*/}
        echo "--- pid=$pid comm=$pn ---" >> "$ML"
        $BB cat $pd/maps >> "$ML" 2>/dev/null
        # 深度快照
        echo "===== T+${U}s #$i =====" >> "$DG"
        echo "-- stat --" >> "$DG"
        $BB cat $pd/stat >> "$DG" 2>/dev/null; echo "" >> "$DG"
        echo "-- status --" >> "$DG"
        $BB cat $pd/status >> "$DG" 2>/dev/null
        echo "-- threads --" >> "$DG"
        for t in $pd/task/*; do
          echo -n "  ${t##*/} " >> "$DG"
          $BB cat $t/comm >> "$DG" 2>&1
          echo -n "   wchan=" >> "$DG"
          $BB cat $t/wchan >> "$DG" 2>&1
          echo >> "$DG"
        done
        # uipc_trace 拷贝
        if [ -f /tmp/uipc_trace.txt ]; then
          cp /tmp/uipc_trace.txt "$W/uipc_trace.txt" 2>/dev/null
          echo "  trace: $($BB wc -c < "$W/uipc_trace.txt" 2>/dev/null) B" >> "$DG"
        fi
        ;;
    esac
  done
  $BB sync 2>/dev/null
  $BB sleep 5
done
echo "== dm.sh done ==" >> "$DG"
DMEOF
  chmod 755 /setting/dm.sh 2>/dev/null
  /bin/sh /setting/dm.sh &
  disown 2>/dev/null || true
  say "  dm.sh 已启动（maps+快照 -> diag17.txt / mapslog.txt）"
  $BB sync 2>/dev/null

  # ★ Kimi 第十四轮 §4：LD_DEBUG 先行
  #   默认 libs,files（捕获 dlopen/open 失败，输出量小能跑到崩溃点）
  #   LDDBG=full 时才加 bindings,symbols（量大，启动阶段就 4MB+，跑不到崩溃）
  # ★★ 2026-10-28 实测修正：libs,files 在 QEMU 里让 App 跑 5 分钟还没走完动态链接
  #   （app.err 停在 libInfraDatabase.so 查找），完全吃掉了 gdb 观测窗口。
  #   ⇒ 默认改为关闭。需要看库加载时显式 LDDBG=1（浅）/ LDDBG=full（深）。
  if [ "${LDDBG:-0}" != "0" ]; then
    if [ "${LDDBG:-0}" = "full" ]; then
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

  # ★ libCoreSync stub / uipc ioctl 探针
  #   libCoreSync_trace.so = stub 三符号 + ioctl 拦截（只记录 fd 指向 uipc 的调用）
  #   优先用它：只占**一个** preload 槽（上游实测两个 preload 库会让 App 卡住）
  if [ "${NOSTUB:-0}" != "1" ]; then
    _pl=""
    for _c in /usr/lib/libCoreSync_trace.so /usr/lib/libCoreSync.so; do
      if [ -f "$_c" ]; then _pl=$_c; break; fi
    done
    if [ -n "$_pl" ]; then
      LD_PRELOAD="$_pl${LD_PRELOAD:+:$LD_PRELOAD}"
      export LD_PRELOAD
      case "$_pl" in
        *_trace.so) say "  LD_PRELOAD=$LD_PRELOAD  (CoreSync stub + uipc ioctl 探针)" ;;
        *)          say "  LD_PRELOAD=$LD_PRELOAD  (libCoreSync stub)" ;;
      esac
    fi
  fi
  [ -z "${LD_PRELOAD:-}" ] && say "  LD_PRELOAD 未设置（无 stub、无 BKT）"

  # ★ 伪造开关：探针把 /dev/uipc 的 0x80000007 直接返回成功（冻结前的最后一个请求）。
  #   默认开；跑对照时用 FAKE7=0 关掉。值可以是次数（如 FAKE7=5）。
  export UIPC_FAKE7="${FAKE7:-1}"
  say "  UIPC_FAKE7=$UIPC_FAKE7  (0x80000007 伪造返回; 0=关)"

  # ★ 关键：把 1/2 接到文件，让 __assert_fail 的断言文本落盘
  # ★ 关 ASLR：appFw.so 基址固定，gdb 断点可复用
  echo 0 > /proc/sys/kernel/randomize_va_space 2>/dev/null
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

/* mini_main.c —— 真机版：原菜单加 "New Menu" 入口。
 *
 * 相对 QEMU 原版（libnativeentry.so）的裁剪：
 *   保留  native_entry 的 4 个 hook（加行 / 进菜单 / 返回 / 绘制）
 *   保留  2 个字体 hook（换标题所需的字串接口）
 *   保留  present 钩子（每帧叠加新菜单）
 *   去掉  27 个观察者 hook、headless 帧导出、UART transport、record 日志
 *
 * 安全设计：
 *   · 每个 hook 前校验原函数前 8-12 字节，不匹配就静默退出
 *   · 用 dl_iterate_phdr 等库加载，不假设加载顺序
 *   · 全程不写任何持久存储
 *   · 出错只影响新菜单，不影响原厂 UI
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nm_log.h"

typedef uintptr_t (*entry_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

/* ---- 原框架提供的基础设施（最小实现）---- */
static inline uint32_t ui_u32(const void *p) { return *(const uint32_t *)p; }
static inline uint16_t ui_u16(const void *p) { return *(const uint16_t *)p; }

static uint64_t headless_native_frames;      /* present 每调一次 +1 */
static uint32_t draw_count;

static uint64_t headless_fingerprint(const void *data, size_t length)
{
    const uint8_t *p = data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < length; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

/* 原框架的 record()：真机上写 stderr，由 im.elf 的日志重定向接管 */
static void record(const char *key, uintptr_t value)
{
    static unsigned budget = 40;
    if (!budget) return;
    budget--;
    nm_log("NM %s=%#lx\n", key, (unsigned long)value);
}

/* ---- 全局基址（native_entry.inc 要用）---- */
static int nm_installed;
static int nm_install_failed;   /* 核心 hook 安装失败，不再重试 */

#include "native_entry.h"
#include "mini_font.h"

/* native_entry.inc 需要的全局变量 */
static uintptr_t object_base, views_base;
static entry_fn original_string_id, original_string_chars;

#include "native_entry.inc"

/* ---- 两个字体钩子的实现（native_entry_title 会调 original_string_chars）---- */
static uintptr_t observe_string_id(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    return original_string_id(a, b, c, d);
}
static uintptr_t observe_string_chars(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    return original_string_chars(a, b, c, d);
}


/* 前置声明 */
static void nm_try_install(void);

/* ---- ★★★ 核心：覆盖 dlopen，在 libObj.so / viewUnified2.so 加载时挂钩 ★★★
 *
 * 上游 libnativeentry.so 就是靠这个机制工作的，不是我之前用的 dl_iterate_phdr 轮询。
 *
 * 原理：
 *   LD_PRELOAD（或 ld.so.preload）让本库先于 libc 进入全局符号表，
 *   于是进程内所有 dlopen 调用都先落到这里。
 *   我们用 dlvsym(RTLD_NEXT, ...) 取回真正的实现，转发后检查 path。
 */
/* ★ 必须导出为 dlopen@GLIBC_2.4。
 *
 * 相机固件链接的是 dlopen@@GLIBC_2.4（见 libdl-2.11.2.so 的 dynsym）。
 * ld.so 解析带版本的重定位时只接受【同版本】的定义；一个无版本标记的
 * dlopen 会被跳过，调用仍走 libdl.so.2。这就是真机上一条 NM dlopen
 * 都没有的原因。
 *
 * 用 .symver 把实现函数改名导出，避免与编译器内建声明冲突。
 */
/* 原版 dlopen 的指针（绕过我们自己的 hook）。
 * 定义在 nm_dlopen_impl 之前，供诊断模式使用。
 */
static void *(*nm_real_dlopen_ptr)(const char *, int);

static void *nm_real_dlopen(const char *path, int flags)
{
    if (!nm_real_dlopen_ptr) {
        nm_real_dlopen_ptr = (void *(*)(const char *, int))
                             dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
        if (!nm_real_dlopen_ptr)
            nm_real_dlopen_ptr = (void *(*)(const char *, int))
                                 dlsym(RTLD_NEXT, "dlopen");
        if (!nm_real_dlopen_ptr) {
            void *hdl = dlopen("libdl.so.2", RTLD_NOW | RTLD_NOLOAD);
            if (hdl) nm_real_dlopen_ptr = (void *(*)(const char *, int))
                                          dlsym(hdl, "dlopen");
        }
    }
    if (!nm_real_dlopen_ptr) return NULL;
    return nm_real_dlopen_ptr(path, flags);
}

void *nm_dlopen_impl(const char *path, int flags);
__asm__(".symver nm_dlopen_impl,dlopen@@GLIBC_2.4");
void *nm_dlopen_impl(const char *path, int flags)
{
    static void *(*native_dlopen)(const char *, int);

    /* ★ 取原版 dlopen 的三重保险。
     *
     * 如果全都拿不到，绝不能返回 NULL —— 那会让 im.elf 的所有 dlopen
     * 都失败，UI 直接崩。所以我们宁可【放弃 hook，直接绕过】：
     * 用 libdl 的 handle 再试最后一次，实在不行就用 __libc_dlopen_mode。
     */
    if (!native_dlopen) {
        /* ① 标准做法 */
        native_dlopen = (void *(*)(const char *, int))
                        dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
        /* ② 不带版本的兜底 */
        if (!native_dlopen)
            native_dlopen = (void *(*)(const char *, int))
                            dlsym(RTLD_NEXT, "dlopen");
        /* ③ 从 libdl 的 handle 里取（绕过 RTLD_NEXT 顺序问题） */
        if (!native_dlopen) {
            void *h = dlopen("libdl.so.2", RTLD_NOW | RTLD_NOLOAD);
            if (h) native_dlopen = (void *(*)(const char *, int))
                                   dlsym(h, "dlopen");
        }
        /* ④ 终极兜底：glibc 内部的 __libc_dlopen_mode（不依赖 libdl） */
        if (!native_dlopen) {
            void *lc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
            if (lc) native_dlopen = (void *(*)(const char *, int))
                                    dlsym(lc, "__libc_dlopen_mode");
        }
        if (native_dlopen)
            nm_log("NM native_dlopen resolved\n");
    }

    /* 全失败：这是最坏情况。我们让 hook 静默透传（返回 NULL 会让上层崩），
     * 所以这里记录一条并尝试用 dlmopen 或者干脆返回 NULL 让上层处理。
     * 实践中 ①~④ 至少有一个会成功。 */
    if (!native_dlopen) {
        nm_log("NM FATAL: cannot resolve native dlopen\n");
        errno = ENOSYS;
        return NULL;
    }

    void *handle = native_dlopen(path, flags);

    /* 每次 dlopen 都记一行，便于诊断 */
    nm_log("NM dlopen path=%s flags=%#x -> %p\n",
           path ? path : "<null>", flags, handle);

    if (handle && path && strstr(path, "libObj.so")) {
        nm_log("NM MATCH libObj\n");
        /* handle 不是库基址，要从任一导出符号反推 */
        Dl_info di;
        void *probe = dlsym(handle, "_ZN9GEN_Label7setTextEj");
        if (!probe) probe = dlsym(handle, "malloc");   /* 兜底：libObj 可能 re-export */
        if (probe && dladdr(probe, &di) && di.dli_fbase) {
            object_base = (uintptr_t)di.dli_fbase;
            nm_log("NM object_base=%#lx\n", (unsigned long)object_base);
            nm_try_install();
        } else {
            nm_log("NM libObj: cannot resolve base\n");
        }
    }
    /* ★ CautionConfig.so 加载时必须重试一次。
     *
     * 这是最容易漏的一环：libObj.so 通常先加载，那时 CautionConfig 还不在，
     * nm_try_install() 会返回 "wait"。等 CautionConfig 真的加载进来时，
     * 如果没有这个分支，就永远没有第二次机会，hook 永远装不上。
     *
     * QEMU 日志确认了这个时序：
     *   NM dlopen ... libObj.so -> NM MATCH libObj -> NM wait: CautionConfig not loaded yet
     */
    if (handle && path && strstr(path, "CautionConfig.so")) {
        nm_log("NM MATCH CautionConfig\n");
        nm_try_install();
    }

    if (handle && path && strstr(path, "viewUnified2.so")) {
        nm_log("NM MATCH viewUnified2\n");
        /* ★ 关键：ne_find_symbol() 依赖 ne_view_handle，
         * 不设它的话 ne_add_row 里所有 ne_find_symbol 调用都返回 NULL，
         * 于是第一道检查 (version->properties->last != 6) 直接 return -1。
         * 这就是 NE_VERSION node=0 / NE_ERROR status=-1 的原因。
         */
        ne_view_handle = handle;
        nm_log("NM ne_view_handle=%p\n", handle);
        void *factory = dlsym(handle, "ViewRootSelectorToInstance");
        Dl_info info;
        if (factory && dladdr(factory, &info) && info.dli_fbase) {
            views_base = (uintptr_t)info.dli_fbase;
            nm_log("NM views_base=%#lx\n", (unsigned long)views_base);
            nm_try_install();
        } else {
            nm_log("NM viewUnified2: no ViewRootSelectorToInstance\n");
        }
    }

    /* ★ 兜底重试：CautionConfig.so 是 viewUnified2.so 的 DT_NEEDED 依赖。
     *
     * 它由 libdl 在 dlopen(viewUnified2) 内部递归加载，【不会】再经过本 hook，
     * 而 viewUnified2.so 本身可能很晚才被 dlopen（QEMU 里全程 0 次）。
     *
     * 所以每次 dlopen 结束时都顺手检查一次 CautionConfig 是否已可解析 ——
     * im.elf 启动阶段会 dlopen 上百个库，总有一次能撞上。
     * 比定时器安全：不打断系统调用、不起线程、不装信号。
     */
    if (!nm_installed && object_base) {
        nm_try_install();
    }

    return handle;
}

/* ---- 库加载回调 ---- */
static int nm_phdr(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz; (void)data;
    const char *path = info->dlpi_name;
    if (!path || !*path || nm_installed) return 0;

    if (strstr(path, "libObj.so"))
        object_base = (uintptr_t)info->dlpi_addr;

    if (strstr(path, "viewUnified2.so")) {
        void *h = dlopen(path, RTLD_NOLOAD | RTLD_LAZY);
        if (h) {
            void *sym = dlsym(h, "ViewRootSelectorToInstance");
            if (sym) {
                Dl_info di;
                if (dladdr(sym, &di) && di.dli_fbase)
                    views_base = (uintptr_t)di.dli_fbase;
            }
        }
    }
    return 0;
}

/* ---- 装 hook 的入口 ---- */
static void nm_try_install(void)
{
    /* ★ views_base 不是必需的。
     *
     * 上游原型里 4 个 native hook 有 3 个挂在 viewUnified2.so 上，
     * 但 QEMU 实测 viewUnified2.so 全程零次 dlopen（它可能只在真机、
     * 或只在某些 UI 路径下才被加载）。
     *
     * CautionConfig 那个 hook（构造函数 0xb8dffa）挂在 CautionConfig.so 上，
     * 用 dlsym(RTLD_DEFAULT) 找，不依赖任何 base，应当独立生效。
     */
    if (nm_installed || nm_install_failed || !object_base) return;

    /* ---- CautionConfig.so 的基址和版本指纹 ----
     *
     * CautionConfig.so 是被 libObj.so 用字符串 "CautionConfig.so" 运行时 dlopen 的
     * （见 libObj.so 的 .rodata：initializeCautionConfig / getCautionConfig /
     *  terminateCautionConfig / "CautionConfig.so"）。
     *
     * 它不走 dl_iterate_phdr，所以我们用 dlsym(RTLD_DEFAULT) 直接找符号。
     * 找不到就说明它还没被加载，稍后再试（watcher 会继续轮询）。
     */
    void *sym = dlsym(RTLD_DEFAULT,
        "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
    if (!sym) {
        nm_log("NM wait: CautionConfig not loaded yet\n");
        return;
    }

    Dl_info di;
    memset(&di, 0, sizeof di);
    if (!dladdr(sym, &di) || !di.dli_fbase) {
        nm_log("NM skip: caution dladdr failed\n");
        return;
    }
    uintptr_t caution = (uintptr_t)di.dli_fbase;

    /* ★ 上游用的指纹值是 0xb8eb21 = 0xb8eb20 | 1（Thumb 位）。
     *   dlsym 返回的指针本身通常已带 Thumb 位，但 ELF 的 st_value 也带，
     *   所以这里【不要】先 & ~1，否则永远差 1。
     */
    uintptr_t sym_off = (uintptr_t)sym - caution;          /* 可能带 Thumb 位 */
    uintptr_t sym_off_even = (uintptr_t)sym & ~(uintptr_t)1;
    sym_off_even -= caution;                                /* 去掉 Thumb 位 */

    nm_log("NM caution sym=%p base=%#lx off=%#lx off_even=%#lx file=%s\n",
           sym, (unsigned long)caution,
           (unsigned long)sym_off, (unsigned long)sym_off_even,
           di.dli_fname ? di.dli_fname : "?");

    /* 接受带/不带 Thumb 位两种写法（0xb8eb21 / 0xb8eb20） */
    if (sym_off != 0xb8eb21 && sym_off_even != 0xb8eb20) {
        nm_log("NM skip: caution anchor mismatch (off=%#lx even=%#lx want 0xb8eb21)\n",
               (unsigned long)sym_off, (unsigned long)sym_off_even);
        return;
    }
    nm_log("NM caution anchor OK\n");

    if (ne_hook(caution + 0xb8dffa, ne_pin_init,
                (void *)ne_init_wrapper, &ne_original_init)) {
        /* 这是最核心的 hook（菜单入口就在这个构造函数里）。
         * 失败说明指纹或偏移不对，重试也没用 —— 但也不能设 nm_installed，
         * 否则再也不试。所以标记成永久失败，避免每次 dlopen 都刷日志。
         */
        nm_log("NM FATAL: caution init hook failed (anchor or patch mismatch)\n");
        nm_install_failed = 1;
        return;
    }
    nm_log("NM hooked CautionConfig init\n");

    /* viewUnified2 相关 hook：有则装，没有就跳过 */
    if (views_base) {
        if (ne_hook(views_base + 0x3014f0, ne_pin_populate,
                    (void *)ne_populate_wrapper, &ne_original_populate) ||
            ne_hook(views_base + 0x2fccc0, ne_pin_main,
                    (void *)ne_main_wrapper, &ne_original_main) ||
            ne_hook(views_base + 0x862130, ne_pin_row,
                    (void *)ne_row_wrapper, &ne_original_row))
            nm_log("NM warn: viewUnified2 hook failed\n");
        else
            nm_log("NM hooked viewUnified2 (3)\n");
    } else {
        nm_log("NM no views_base; skipping viewUnified2 hooks\n");
    }

    /* 字体钩子：换标题 */
    {
        if (ne_hook(object_base + 0x626538,
                    (const unsigned char[]){0x90,0xb5,0x83,0xb0,0x00,0xaf,0x04,0x46},
                    (void *)observe_string_id, &original_string_id))
            nm_log("NM warn: string_id hook failed\n");
        if (ne_hook(object_base + 0x6266d0,
                    (const unsigned char[]){0xf0,0xb5,0x00,0xf1,0x98,0x06,0x85,0xb0},
                    (void *)observe_string_chars, &original_string_chars))
            nm_log("NM warn: string_chars hook failed\n");
    }

    /* ★ 关键：必须同时设置 native_entry.inc 的 ne_modules_installed。
     *
     * 这两个是不同的变量：
     *   nm_installed         —— 控制 nm_try_install 是否还重试
     *   ne_modules_installed —— 控制 ne_init_wrapper 里的门禁：
     *
     *     if (ne_modules_installed && !ne_installed && caller == views_base + 0x301921)
     *         ne_add_row(root);
     *
     * 少了这一句，hook 装得再好，ne_add_row 也永远不会被调用，
     * 菜单项就不会出现。这是之前一直"装上了但没效果"的根因。
     */
    /* ★ 同样必须回写 ne_caution。
     *
     * ne_add_row 里三处用到它：
     *   if (version->vptr != ne_caution + 0xfab968) return -3;
     *   clone                = (void *)(ne_caution + 0xb8eafd);
     *   construct_properties = (void *)(ne_caution + 0xb8ed3d);
     *
     * 这个函数里算的是局部变量 caution，不回写的话 ne_caution 仍是 0，
     * ne_add_row 要么在 -3 处失败，要么跳到 0xb8eafd 这种裸偏移地址上。
     */
    ne_caution = caution;

    ne_modules_installed = 1;

    nm_installed = 1;
    nm_log("NM installed object=%#lx views=%#lx modules=%d\n",
           (unsigned long)object_base, (unsigned long)views_base,
           ne_modules_installed);
}

/* ---- 单实例锁 ----
 *
 * LD_PRELOAD 是环境变量，会被 exec 继承。如果 im.elf 派生并 exec 了别的
 * 程序，那个程序也会加载本库。没有锁就会重复 fork 轮询进程。
 * 用抽象 socket 名做锁：进程退出时内核自动释放，不会残留。
 */
#include <sys/socket.h>
#include <sys/un.h>

static int nm_lock_fd = -1;

static int nm_acquire_lock(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path + 1, "zve10_newmenu");
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    nm_lock_fd = fd;
    return 0;
}

/* ---- 轮询线程 ---- */
/* 从 /proc/PID/maps 里找某个库的基址。
 * 返回 0 表示没找到。 */
static uintptr_t nm_base_from_maps(pid_t pid, const char *needle)
{
    char path[64];
    int n = snprintf(path, sizeof path, "/proc/%d/maps", (int)pid);
    if (n <= 0) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    static char buf[64 << 10];
    int len = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (len <= 0) return 0;
    buf[len] = 0;

    uintptr_t found = 0;
    char *line = buf;
    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        if (strstr(line, needle)) {
            /* 行首是 "start-end perms ..." */
            unsigned long start = 0;
            if (sscanf(line, "%lx-", &start) == 1) {
                found = (uintptr_t)start;
                if (eol) *eol = '\n';
                break;
            }
        }
        if (!eol) break;
        *eol = '\n';
        line = eol + 1;
    }
    return found;
}

static void nm_watch_parent(pid_t parent)
{
    nm_log("NM watcher start parent=%d\n", (int)parent);

    int report = 0;
    for (int i = 0; i < 3600 && !nm_installed; i++) {
        usleep(500 * 1000);

        if (!object_base)
            object_base = nm_base_from_maps(parent, "libObj.so");
        if (!views_base)
            views_base = nm_base_from_maps(parent, "viewUnified2.so");

        if ((i % 10) == 0 && report < 60) {
            report++;
            nm_log("NM watch i=%d obj=%#lx views=%#lx\n", i,
                   (unsigned long)object_base, (unsigned long)views_base);
        }
        if (object_base && views_base) {
            nm_log("NM both found, but cannot hook from child process\n");
            break;
        }
    }
    nm_log("NM watcher exit\n");
}

/* ---- 构造函数 ---- */
/* ---- 定时器回调：在本进程内轮询 ----
 *
 * 不能用 fork：子进程不共享地址空间，改不了父进程的函数指针。
 * 不能用 pthread_create：在 ld.so 构造函数阶段会死锁（QEMU 实测挂死）。
 *
 * 用 setitimer(ITIMER_REAL) + SIGALRM：信号处理函数在【本进程】里执行，
 * 能直接改函数指针，也没有线程/栈的问题。
 */
static volatile int nm_poll_ticks = 0;

static void nm_on_alarm(int sig)
{
    (void)sig;
    nm_poll_ticks++;

    if (nm_installed) return;

    /* 每 2 tick（1 秒）真正查一次，避免信号里做太多事 */
    if ((nm_poll_ticks & 1) != 0) return;

    dl_iterate_phdr(nm_phdr, NULL);
    nm_try_install();

    if (!nm_installed && (nm_poll_ticks % 20) == 0 && nm_poll_ticks < 400) {
        nm_log("NM tick=%d obj=%#lx views=%#lx\n", nm_poll_ticks,
               (unsigned long)object_base, (unsigned long)views_base);
    }
}

static void nm_start_timer(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = nm_on_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &sa, NULL) != 0) {
        nm_log("NM sigaction FAILED errno=%d\n", errno);
        return;
    }

    struct itimerval it;
    memset(&it, 0, sizeof it);
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 500 * 1000;   /* 每 500 ms */
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
        nm_log("NM setitimer FAILED errno=%d\n", errno);
        return;
    }
}

/* ---- 判断本进程是否是 UI 主进程 ----
 *
 * 只有在 im.elf 里才需要装 hook。其他进程（init/busybox/wpa_supplicant/...）
 * 既没有 libObj.so 也没有 CautionConfig.so，给它们起定时器只会
 * 用 SIGALRM 打断它们的系统调用 —— 实测会导致菜单卡死。
 */
static int nm_is_ui_process(void)
{
    char buf[128];
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;

    /* cmdline 是 NUL 分隔，第一段是可执行文件路径 */
    int end = 0;
    while (end < n && buf[end] != 0) end++;
    buf[end] = 0;

    return strstr(buf, "im.elf") != NULL;
}

__attribute__((constructor))
static void nm_boot(void)
{
    /* ★ 关键：只有 UI 主进程才工作。
     *
     * 实测过三种轮询方式，都有问题：
     *   fork          → 子进程改不了父进程的函数指针
     *   pthread_create→ ld.so 构造函数阶段死锁
     *   setitimer     → SIGALRM 打断所有进程的系统调用，菜单卡死
     *
     * 所以现在：
     *   1. 先判断是不是 im.elf，不是就立刻返回（零开销、零副作用）
     *   2. 是的话只在本进程内装 timer
     */
    /* ★ 启动诊断：把关键环境一次性打进日志。
     *
     * 真机只有一次机会看到这些（重启就换新进程），
     * 而"库进得去但 hook 装不上"这类问题全靠这些信息定位。
     */
    {
        char exe[256];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) exe[n] = 0; else strcpy(exe, "?");

        const char *envp_ = getenv("LD_PRELOAD");
        int has_etc_preload = (access("/etc/ld.so.preload", R_OK) == 0);

        nm_log("NM === boot pid=%d exe=%s ===\n", (int)getpid(), exe);
        nm_log("NM LD_PRELOAD=%s\n", envp_ ? envp_ : "<unset>");
        nm_log("NM /etc/ld.so.preload readable=%d\n", has_etc_preload);
    }

    if (!nm_is_ui_process()) return;

    int have_lock = (nm_acquire_lock() == 0);
    nm_log("NM ui pid=%d lock=%d\n", (int)getpid(), have_lock);

    /* ★ 安全模式：只记录库加载成功，不装任何 hook。
     *
     * 用 /z_safe 文件控制（在 initramfs 里，不依赖分区挂载）：
     *   /z_safe 存在 → 只观测，不改菜单（第一阶段验证用）
     *   /etc/nm_safe 不存在 → 正常装 hook
     *
     * 这样第一次上真机时，如果库有问题至少能看到日志，
     * 而且不会因为 hook 出错导致 UI 卡死。
     */
    if (access("/z_safe", F_OK) == 0) {
        nm_log("NM SAFE MODE: library loaded OK, hooks DISABLED\n");
        /* 只做一次库扫描，记录能看到什么 */
        dl_iterate_phdr(nm_phdr, NULL);
        nm_log("NM safe-mode object=%#lx views=%#lx\n",
               (unsigned long)object_base, (unsigned long)views_base);
        return;
    }

    /* ★ 只在构造函数里试一次，不装任何定时器。
     *
     * 之前用 setitimer(ITIMER_REAL) 每 500ms 发 SIGALRM，
     * 打断了 im.elf 主线程的系统调用，导致菜单卡死（真机实测）。
     *
     * 而 dlopen hook 已经能覆盖所有需要的时机：
     *   · libObj.so 是运行时 dlopen 的（不在 DT_NEEDED 里）
     *   · viewUnified2.so / CautionConfig.so 也是
     * 所以 hook 会在它们加载时自动触发 nm_try_install()。
     *
     * 构造函数里只做一次静态扫描（dl_iterate_phdr），
     * 万一某些库已经在内存里（比如 libc 之后的加载顺序），也能捕获。
     */
    dl_iterate_phdr(nm_phdr, NULL);
    nm_try_install();

    if (nm_installed) {
        nm_log("NM installed at ctor\n");
    } else {
        nm_log("NM waiting for dlopen (no timer, hook-driven)\n");

        /* ★ 诊断模式：/z_force 存在时，主动 dlopen viewUnified2.so，
         * 让 4 个 hook 有机会装上。
         *
         * 用途：QEMU 里 CP 初始化不完整，UI 走不到加载视图库那一步，
         * 导致 hook 永远装不上、无法验证 ne_add_row 逻辑。
         * 这个开关让库自己拉起来，绕过 CP 依赖。
         *
         * 真机上不需要这个开关（UI 会自己加载）。
         */
        if (access("/z_force", F_OK) == 0) {
            nm_log("NM FORCE: /z_force present, dlopening viewUnified2.so\n");
            void *h = nm_real_dlopen("viewUnified2.so", RTLD_NOW | RTLD_GLOBAL);
            nm_log("NM FORCE: viewUnified2 handle=%p\n", h);
            if (h) {
                void *ctor = dlsym(h, "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
                nm_log("NM FORCE: caution ctor=%p\n", ctor);
                void *factory = dlsym(h, "ViewRootSelectorToInstance");
                nm_log("NM FORCE: ViewRootSelectorToInstance=%p\n", factory);
            }
            nm_try_install();
            nm_log("NM FORCE: after try_install installed=%d modules=%d caution=%#lx views=%#lx\n",
                   nm_installed, ne_modules_installed,
                   (unsigned long)ne_caution, (unsigned long)views_base);
        }
    }
}



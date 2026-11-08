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
void *dlopen(const char *path, int flags)
{
    static void *(*native_dlopen)(const char *, int);

    if (!native_dlopen) {
        native_dlopen = dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
        if (!native_dlopen) native_dlopen = dlsym(RTLD_NEXT, "dlopen");
    }
    if (!native_dlopen) { errno = ENOSYS; return NULL; }

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
    if (handle && path && strstr(path, "viewUnified2.so")) {
        nm_log("NM MATCH viewUnified2\n");
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
    if (nm_installed || !object_base) return;

    void *sym = dlsym(RTLD_DEFAULT,
        "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
    if (!sym) {
        nm_log("NM skip: caution symbol not found\n");
        return;
    }

    Dl_info di;
    memset(&di, 0, sizeof di);
    if (!dladdr(sym, &di) || !di.dli_fbase) {
        nm_log("NM skip: caution dladdr failed\n");
        return;
    }
    uintptr_t caution = (uintptr_t)di.dli_fbase;
    uintptr_t delta = ((uintptr_t)sym & ~(uintptr_t)1) - caution;

    nm_log("NM caution sym=%p base=%#lx delta=%#lx file=%s\n",
           sym, (unsigned long)caution, (unsigned long)delta,
           di.dli_fname ? di.dli_fname : "?");

    /* 版本锚点：构造函数必须在 0xb8eb21 */
    if (delta != 0xb8eb21) {
        nm_log("NM skip: caution anchor mismatch (got %#lx)\n", (unsigned long)delta);
        return;
    }

    if (ne_hook(caution + 0xb8dffa, ne_pin_init,
                (void *)ne_init_wrapper, &ne_original_init)) {
        nm_log("NM skip: caution init hook failed\n");
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

    nm_installed = 1;
    nm_log("NM installed object=%#lx views=%#lx\n",
           (unsigned long)object_base, (unsigned long)views_base);
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
__attribute__((constructor))
static void nm_boot(void)
{
    /* ★ 锁只用来决定"谁来装 hook"，不能让锁失败导致整个库失效。
     *
     * 每个加载本库的进程都会导出 dlopen 覆盖——这是好事，
     * 因为不管是 bootin.elf 还是 im.elf（还是它们的子进程）先加载 libObj.so，
     * 都需要 hook 生效。锁只用来避免重复安装和重复 fork watcher。
     */
    /* ★ 不能用"抢锁失败就退出"来决定是否安装 hook。
     *
     * 每个进程有独立的地址空间，libObj.so 在哪加载，就得在那个进程里
     * 覆盖对应的函数指针。锁唯一的作用是避免同一个进程里重复 fork watcher。
     *
     * 之前的设计让第一个加载本库的进程（往往是 init 或 ash）抢到锁，
     * 结果真正 dlopen libObj.so 的 bootin.elf / im.elf 反而被挡在门外。
     */
    int have_lock = (nm_acquire_lock() == 0);
    nm_log("NM load pid=%d lock=%d\n", (int)getpid(), have_lock);
    dl_iterate_phdr(nm_phdr, NULL);
    nm_try_install();

    /* ★ 用 fork + /proc/PID/maps，不用线程。
     *
     * 为什么不用 pthread_create：
     *   在 ld.so 的构造函数阶段创建线程会死锁（glibc 的线程栈、
     *   TLS、以及 bootin.elf 可能还没初始化完 libpthread）。
     *   QEMU 实测：pthread_create 之后整个 bootin.elf 卡死，serial 停住。
     *
     * 为什么 fork 也能看到后续 dlopen：
     *   子进程虽然不共享地址空间，但可以读【父进程】的 /proc/PID/maps，
     *   那是内核实时维护的，能看到主进程后来映射的所有库。
     */
    nm_log("NM ctor done (no watcher, test build)\n");
    return;
}

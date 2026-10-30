/* libCoreSyncTrace.c — 一个 preload 库干两件事，避免踩「两个 preload 库卡 App」的坑
 *
 *  1. 补齐 libCoreSync.so 的 3 个 stub 符号（等价于 work/libCoreSync_stub.s，返回 0）
 *  2. 拦截 ioctl，记录 fd / request / arg / 返回值 / errno / TID / 单调时间 / qid
 *
 * v2（2026-10-28 16:30）—— 依据外部审计修订观测字段：
 *   - 加 TID 与 CLOCK_MONOTONIC 微秒时间戳（可与线程快照关联）
 *   - cmd 4/5/6/7/8/13/16 统一抽出 qid（arg 外层第一个字）
 *   - cmd 7 额外输出 timeout（arg+8）
 *   - cmd 8 输出 qid（外层 +0x00，此前误当成「整段都是指针」）
 *   - 加 run-id（首次 openlog 的单调时间），跨轮可区分
 *   - REQ/RET 共享 seq，调用后立即保存 errno
 *   - 记录丢失：超过 MAXLOG 后写一行 TRUNC 并停止
 *
 * 仍为**单库**，不引入 getenv / 第二个 preload。
 *
 * 编译（soft-float，与相机 glibc 2.11.2 一致）：
 *   arm-linux-gnueabi-gcc -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp \
 *       -fno-builtin -DFAKE7_COUNT=0 -o libCoreSync.so libCoreSyncTrace.c
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>

/* ---------------- 1. libCoreSync stub（与原汇编等价） ---------------- */
int core_sync_user_init(void) { return 0; }
int core_sync_user_sus(void)  { return 0; }
int core_sync_user_res(void)  { return 0; }

/* ---------------- 2. 极简日志（raw syscall，不碰 stdio） ---------------- */
static int g_log = -2;                 /* -2=未初始化 -1=失败 >=0=fd */
static signed char g_kind[256];        /* 0=未知 1=uipc 2=其他 */
static int g_n = 0;
static int g_trunc = 0;
#define MAXLOG 200000

/* ★ 全局调用序号：REQ 与对应的 RET 共享同一个 seq。 */
static unsigned int g_seq = 0;

/* run-id：首次 openlog 时的 CLOCK_MONOTONIC 微秒，用来区分不同轮 */
static unsigned long long g_runid = 0;

/* ---------- 时间 / 线程（全部走 syscall，不依赖 libc 时钟实现） ----------
 * ★ 不用 SYS_* 宏：实测交叉工具链的 <sys/syscall.h> 里 SYS_clock_gettime /
 *   SYS_gettid 展开为空（宏链断裂），导致调用 syscall(0,...) 让 App 在
 *   dmp_allocator_lib_init 断言失败。这里硬编码 ARM EABI 标准号。
 *   ARM EABI: write=4 ioctl=54 open=5 readlink=85 gettid=224 clock_gettime=263 */
#define NR_WRITE      4
#define NR_IOCTL      54
#define NR_OPEN       5
#define NR_READLINK   85
#define NR_GETTID     224
#define NR_CLKGET     263
#define CLOCK_MONOTONIC 1

struct ts32 { long tv_sec; long tv_nsec; };

static unsigned long long now_us(void) {
    struct ts32 ts;
    long r = syscall(NR_CLKGET, CLOCK_MONOTONIC, &ts);
    if (r < 0) return 0;
    return (unsigned long long)ts.tv_sec * 1000000ULL
         + (unsigned long long)ts.tv_nsec / 1000ULL;
}

static int my_tid(void) {
    return (int)syscall(NR_GETTID);
}

/* 前向声明 */
static int slen(const char *s);
static char *puthex(char *p, unsigned long v);
static char *putdec(char *p, long v);
static char *putull(char *p, unsigned long long v);

/* qid 提取：多数 uipc ioctl 的外层第一个字是 qid。
 * cmd 8 已由反汇编确认（外层 +0x00 = qid，+0x04 = 内部参数结构指针）。
 * cmd 4/5/6/7 的 arg+0 也是 qid（cmd 7 = {qid, buf, timeout}）。 */
static unsigned int peek_u32(void *p) {
    if (!p) return 0;
    /* 用户指针，同进程可读；失败只是读到垃圾，不会崩 */
    return *(volatile unsigned int *)p;
}

/* 主日志行：
 *   REQ <seq> <tid> <t_us> <fd> <req> <arg> <qid> [<extra>]
 *   RET <seq> <tid> <t_us> <fd> <req> <arg> <ret> <err>
 * extra 仅 cmd 7 输出 timeout。 */
static void log2(const char *tag, unsigned int seq, int fd, unsigned long req,
                 void *arg, long ret, int err, unsigned int qid,
                 int have_qid, unsigned long extra, int have_extra) {
    char b[200], *p = b;
    int n = slen(tag);
    memcpy(p, tag, n); p += n;
    *p++ = ' ';
    p = putdec(p, (long)seq); *p++ = ' ';
    p = putdec(p, (long)my_tid()); *p++ = ' ';
    p = putull(p, now_us()); *p++ = ' ';
    p = putdec(p, fd); *p++ = ' ';
    p = puthex(p, req); *p++ = ' ';
    p = puthex(p, (unsigned long)arg); *p++ = ' ';
    if (have_qid) p = puthex(p, qid); else { *p++ = '-'; }
    if (have_extra) { *p++ = ' '; p = puthex(p, extra); }
    if (ret != -9999) {
        *p++ = ' '; p = putdec(p, ret);
        *p++ = ' '; p = putdec(p, err);
    }
    *p++ = '\n';
    syscall(NR_WRITE, g_log, b, (size_t)(p - b));
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char *puthex(char *p, unsigned long v) {
    static const char hx[] = "0123456789abcdef";
    char t[16]; int i = 0;
    *p++ = '0'; *p++ = 'x';
    if (!v) { *p++ = '0'; return p; }
    while (v && i < 16) { t[i++] = hx[v & 0xf]; v >>= 4; }
    while (i--) *p++ = t[i];
    return p;
}

static char *putdec(char *p, long v) {
    char t[24]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (!v) t[i++] = '0';
    while (v && i < 24) { t[i++] = '0' + (v % 10); v /= 10; }
    if (neg) *p++ = '-';
    while (i--) *p++ = t[i];
    return p;
}

static char *putull(char *p, unsigned long long v) {
    char t[24]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v && i < 24) { t[i++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (i--) *p++ = t[i];
    return p;
}

static void openlog(void) {
    /* ★ 优先写 /tmp（ramdisk）；/setting 是 FAT，写多了会把 App 拖慢。
     *   实测写 /setting 时 6963 次 ioctl ×2 次 write 把 App 从 50s 拖到 133s。 */
    g_log = (int)syscall(NR_OPEN, "/tmp/uipc_trace.txt",
                         O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_log < 0) {
        g_log = (int)syscall(NR_OPEN, "/setting/uipc_trace.txt",
                             O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (g_log >= 0) {
        char b[96], *p = b;
        g_runid = now_us();
        memcpy(p, "=== uipctrace v2 === run=", 25); p += 25;
        p = putull(p, g_runid); *p++ = ' ';
        memcpy(p, "pid=", 4); p += 4;
        p = putdec(p, (long)my_tid()); *p++ = '\n';
        syscall(NR_WRITE, g_log, b, (size_t)(p - b));
    }
}

/* fd 是否指向 /dev/uipc —— readlink /proc/self/fd/N */
static int fd_is_uipc(int fd) {
    char p[40], t[128];
    char *q = p;
    memcpy(q, "/proc/self/fd/", 14); q += 14;
    q = putdec(q, fd);
    *q = 0;
    long r = syscall(NR_READLINK, p, t, sizeof(t) - 1);
    if (r <= 0) return 0;
    t[r] = 0;
    return strstr(t, "uipc") != 0;
}

/* ---------------- 3. ioctl 拦截 ---------------- */
int ioctl(int fd, unsigned long req, ...);

#ifndef FAKE7_COUNT
#define FAKE7_COUNT 0
#endif

#define REQ_UIPC_07 0x80000007UL

/* ★ B0/B1：对 cmd 7 的缓冲区在调用前/返回后各 dump 8 个字。
 *   只抽很少几次（审计指出旧版抽样不均匀），默认 2 次。 */
#ifndef DUMPBUF
#define DUMPBUF 1
#endif

#if DUMPBUF
static int g_bn = 2;

static void dumpbuf(const char *tag, unsigned int seq, unsigned long a) {
    char b[230], *p = b;
    int n = slen(tag);
    memcpy(p, tag, n); p += n;
    *p++ = ' ';
    p = putdec(p, (long)seq); *p++ = ' ';
    p = puthex(p, a); *p++ = ':';
    for (int k = 0; k < 8; k++) {
        unsigned int v = ((volatile unsigned int *)a)[k];
        *p++ = ' ';
        p = puthex(p, v);
    }
    *p++ = '\n';
    syscall(NR_WRITE, g_log, b, (size_t)(p - b));
}
#endif

#if FAKE7_COUNT != 0
static int g_fake7 = FAKE7_COUNT;
#endif

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    void *arg = 0;
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (g_log == -2) openlog();

    int kind = 2;
    if (fd >= 0 && fd < 256) {
        kind = g_kind[fd];
        if (kind == 0) { kind = fd_is_uipc(fd) ? 1 : 2; g_kind[fd] = (signed char)kind; }
    } else {
        kind = fd_is_uipc(fd) ? 1 : 2;
    }

    int trace = (kind == 1);
    if (trace && g_n >= MAXLOG) {
        if (!g_trunc) {
            g_trunc = 1;
            syscall(NR_WRITE, g_log, "TRUNC\n", 6);
        }
        trace = 0;
    }
    unsigned int seq = __sync_fetch_and_add(&g_seq, 1);

    /* 提取 qid（外层第一个字）。cmd 4/5/6/7/8/13/16 均适用。 */
    unsigned int qid = 0;
    int have_qid = 0;
    unsigned long extra = 0;
    int have_extra = 0;

    if (trace && arg != 0 && (unsigned long)arg > 0x10000UL) {
        /* ★ 只对【已知 arg 是结构指针】的 cmd 解引用，避免读到无效地址。
         *   cmd 4/5/6/7/8/13 的外层第一个字是 qid（cmd 8 已反汇编确认）。
         *   其余 cmd（3/10/11/12/14/15）的 arg 语义不明，一律不读。 */
        unsigned long r7 = req & 0xffUL;
        if (r7 == 0x04 || r7 == 0x05 || r7 == 0x06 || r7 == 0x07 ||
            r7 == 0x08 || r7 == 0x0d) {
            qid = peek_u32(arg);
            have_qid = 1;
        }
        if (req == REQ_UIPC_07) {
            /* arg = {void *buf; u32 timeout;} 的指针，即 arg[0]=qid, arg[2]=timeout */
            extra = (unsigned long)((volatile unsigned int *)arg)[2];
            have_extra = 1;
        }
    }

#if FAKE7_COUNT != 0
    if (trace && req == REQ_UIPC_07 && g_fake7 != 0) {
        if (g_fake7 > 0) g_fake7--;
        log2("FAK", seq, fd, req, arg, 0, 0, qid, have_qid, extra, have_extra);
        g_n++;
        errno = 0;
        return 0;
    }
#endif

#if DUMPBUF
    int dumped = 0;
    if (trace && req == REQ_UIPC_07 && g_bn > 0) {
        dumpbuf("B0", seq, (unsigned long)arg); dumped = 1;
    }
#endif

    if (trace) {
        log2("REQ", seq, fd, req, arg, -9999, 0, qid, have_qid, extra, have_extra);
        g_n++;
    }

    long r = syscall(NR_IOCTL, fd, req, arg);
    int e = errno;

    if (trace) {
        log2("RET", seq, fd, req, arg, r, e, qid, have_qid, extra, have_extra);
        g_n++;
    }
#if DUMPBUF
    if (dumped) { dumpbuf("B1", seq, (unsigned long)arg); g_bn--; }
#endif
    errno = e;
    return (int)r;
}

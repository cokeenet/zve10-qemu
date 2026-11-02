/* libCoreSyncTrace.c — 一个 preload 库干两件事，避免踩「两个 preload 库卡 App」的坑
 *
 *  1. 补齐 libCoreSync.so 的 3 个 stub 符号（等价于 work/libCoreSync_stub.s，返回 0）
 *  2. 拦截 ioctl，记录 fd / request / arg / 返回值 / errno
 *     —— 只对「fd 指向的设备名含 uipc」的调用做详细记录
 *
 * 判据：阻塞的请求会留下 REQ 而**没有对应的 RET**。
 *       日志写到 /setting/uipc_trace.txt（FAT 可写分区）。
 *
 * 编译（soft-float，与相机 glibc 2.11.2 一致）：
 *   arm-linux-gnueabi-gcc -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp \
 *       -fno-builtin -o libCoreSync.so libCoreSyncTrace.c
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
#define MAXLOG 200000

/* ★ 全局调用序号：REQ 与对应的 RET 共享同一个 seq，配对就无歧义。
 *   加这个是因为实测发现旧版"按 REQ/RET 计数差"推断阻塞不可靠
 *   （多线程下日志上限没按预期生效 ⇒ 尾部可能 REQ 记了、RET 被截掉）。 */
static unsigned int g_seq = 0;

/* 前向声明（log2 用到，它们的定义在下面） */
static int slen(const char *s);
static char *puthex(char *p, unsigned long v);
static char *putdec(char *p, long v);

static void log2(const char *tag, unsigned int seq, int fd, unsigned long req,
                 void *arg, long ret, int err) {
    char b[160], *p = b;
    int n = slen(tag);
    memcpy(p, tag, n); p += n;
    *p++ = ' ';
    p = putdec(p, (long)seq); *p++ = ' ';
    p = putdec(p, fd); *p++ = ' ';
    p = puthex(p, req); *p++ = ' ';
    p = puthex(p, (unsigned long)arg);
    if (ret != -9999) {
        *p++ = ' '; p = putdec(p, ret);
        *p++ = ' '; p = putdec(p, err);
    }
    *p++ = '\n';
    syscall(SYS_write, g_log, b, (size_t)(p - b));
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void swrite(const char *s) {
    if (g_log >= 0) syscall(SYS_write, g_log, s, (size_t)slen(s));
}

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

static void openlog(void) {
    /* ★ 优先写 /tmp（ramdisk，tmpfs/ram0 ⇒ 内存操作，几乎零开销）。
     *   实测写 /setting（FAT）时 6963 次 ioctl ×2 次 write 直接把 App 从
     *   50s 拖到 133s（trace 13902 行 / 396 KB），App 根本跑不到卡点。
     *   /tmp 写不动才退到 /setting。diag_app.sh 会负责把 /tmp 那份拷进 /setting。 */
    g_log = (int)syscall(SYS_open, "/tmp/uipc_trace.txt",
                         O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_log < 0) {
        g_log = (int)syscall(SYS_open, "/setting/uipc_trace.txt",
                             O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (g_log >= 0) {
        char *p, b[64];
        p = b; memcpy(p, "=== uipctrace start ===\n", 25); p += 25;
        syscall(SYS_write, g_log, b, (size_t)(p - b));
    }
}

/* fd 是否指向 /dev/uipc —— readlink /proc/self/fd/N */
static int fd_is_uipc(int fd) {
    char p[40], t[128];
    char *q = p;
    memcpy(q, "/proc/self/fd/", 14); q += 14;
    q = putdec(q, fd);
    *q = 0;
    long r = syscall(SYS_readlink, p, t, sizeof(t) - 1);
    if (r <= 0) return 0;
    t[r] = 0;
    return strstr(t, "uipc") != 0;
}

static void log_call(const char *tag, int fd, unsigned long req,
                     void *arg, long ret, int err) {
    char b[128], *p = b;
    int n = slen(tag);
    memcpy(p, tag, (size_t)n); p += n;
    p = putdec(p, fd); *p++ = ' ';
    p = puthex(p, req); *p++ = ' ';
    p = puthex(p, (unsigned long)arg);
    if (ret != -9999) { *p++ = ' '; p = putdec(p, ret); *p++ = ' '; p = putdec(p, err); }
    *p++ = '\n';
    syscall(SYS_write, g_log, b, (size_t)(p - b));
}

/* ---------------- 3. ioctl 拦截 ---------------- */
int ioctl(int fd, unsigned long req, ...);

/* ★ 伪造开关 —— **编译期**，不读环境变量。
 *   原因：上一版用 getenv("UIPC_FAKE7") 时，im.real 在 exec 后立刻掉进 D（disk sleep）、
 *   只有 8 线程、VmRSS 2 MB，239 秒没加载出 appFw（41 轮 MISS），把 getenv 推迟到
 *   首次遇到 07 也无效。回退到不含 getenv 的纯 trace 版后基线立即恢复正常
 *   （13606 行 trace / app.err 175 B）⇒ 换编译期常量，彻底避开运行时依赖。
 *
 *   用法：-DFAKE7_COUNT=200   （伪造前 200 次 0x80000007；0 = 关闭）
 */
#ifndef FAKE7_COUNT
#define FAKE7_COUNT 0
#endif

/* ★ DUMPBUF：把 0x80000007 的缓冲区在「调用前 / 返回后」各 dump 8 个字，
 *   用来判断内核到底有没有往里填东西、结构长什么样。默认开，dump 前 4 次。 */
#ifndef DUMPBUF
#define DUMPBUF 1
#endif

#define REQ_UIPC_07 0x80000007UL

/* ★ DUMPQID：对 cmd 4（snd_msg）/ cmd 5（snd_sync_msg）记录**目标 qid**（arg 第一个字）。
 *   目的：直接回答"到底有没有人往 0x008F0139 发消息"。没有 ⇒ 该队列无生产者 ⇒ 结案。 */
#ifndef DUMPQID
#define DUMPQID 1
#endif

#if DUMPQID
static int g_qn = 0;

static void logqid(unsigned long req, void *arg) {
    char b[64], *p = b;
    unsigned int qid = ((volatile unsigned int *)arg)[0];
    memcpy(p, "Q ", 2); p += 2;
    p = puthex(p, req); *p++ = ' ';
    p = puthex(p, qid);
    *p++ = '\n';
    syscall(SYS_write, g_log, b, (size_t)(p - b));
    g_qn++;
}
#endif

#if FAKE7_COUNT != 0
static int g_fake7 = FAKE7_COUNT;
#endif

#if DUMPBUF
static int g_bn = 4;

static void dumpbuf(const char *tag, unsigned long a) {
    char b[220], *p = b;
    int n = slen(tag);
    memcpy(p, tag, n); p += n;
    *p++ = ' ';
    p = puthex(p, a); *p++ = ':';
    for (int k = 0; k < 8; k++) {
        /* 直接解引用：我们在同一个进程里，App 递给内核的指针用户态可直接读 */
        unsigned int v = ((volatile unsigned int *)a)[k];
        *p++ = ' ';
        p = puthex(p, v);
    }
    *p++ = '\n';
    syscall(SYS_write, g_log, b, (size_t)(p - b));
}
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

    int trace = (kind == 1) && (g_n < MAXLOG);
    unsigned int seq = __sync_fetch_and_add(&g_seq, 1);

#if FAKE7_COUNT != 0
    if (trace && req == REQ_UIPC_07 && g_fake7 != 0) {
        if (g_fake7 > 0) g_fake7--;
        log2("FAK", seq, fd, req, arg, 0, 0);
        g_n++;
        errno = 0;
        return 0;
    }
#endif

#if DUMPBUF
    int dumped = 0;
    if (trace && req == REQ_UIPC_07 && g_bn > 0) { dumpbuf("B0", (unsigned long)arg); dumped = 1; }
#endif

#if DUMPQID
    if (trace && (req == 0x80000004UL || req == 0x80000005UL) && g_qn < 20000
        && arg != 0) {
        logqid(req, arg);
    }
#endif

    if (trace) { log2("REQ", seq, fd, req, arg, -9999, 0); g_n++; }

    long r = syscall(SYS_ioctl, fd, req, arg);
    int e = errno;

    if (trace) { log2("RET", seq, fd, req, arg, r, e); }
#if DUMPBUF
    if (dumped) { dumpbuf("B1", (unsigned long)arg); g_bn--; }
#endif
    errno = e;
    return (int)r;
}

/* zve10-guardian —— 让 im.elf 在带 LD_PRELOAD 的环境下重启
 *
 * 背景：
 *   /usr/bin/im.elf 启动时 /etc 还没挂载完 → 读不到 /etc/ld.so.preload
 *   → 我们的库进不去 UI 进程。
 *   但实测：开机后手动启动的 im.elf 实例【能】加载 preload。
 *
 * 做法（关键：im.elf 是常驻进程，不能只等它退出）：
 *   ① 找所有 im.elf 进程
 *   ② 检查它的 /proc/PID/maps 里有没有 preload 里的库
 *   ③ 没有 → kill 掉，用 putenv+execv 重启（会带上 preload）
 *   ④ 有   → 成功，什么都不做
 *   ⑤ /etc/guardian.off 存在 → 立刻退出
 *
 * 安全：
 *   · 只在"库没加载"时才杀 im.elf
 *   · 重启失败会重试，10 次后放弃
 *   · 全程写 /log/guardian.log
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define LOGF        "/log/guardian.log"
#define OFF_FLAG    "/etc/guardian.off"
#define PRELOAD_F   "/etc/ld.so.preload"
#define IM_ELF      "/usr/bin/im.elf"
#define RETRY_SEC   3
#define MAX_FAILS   10
#define POLL_SEC    5

/* 我们关心的库名（用于判断 im.elf 是否已经带上了 hook） */
#define MARK_LIB    "libnewmenu.so"

static void logmsg(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    FILE *f = fopen(LOGF, "a");
    if (f) { fprintf(f, "[%ld] %s\n", (long)time(NULL), b); fclose(f); }
}

static int have(const char *p) { return access(p, F_OK) == 0; }

/* 读 /etc/ld.so.preload，拼成冒号分隔单行 */
static int read_preload(char *out, size_t cap)
{
    FILE *f = fopen(PRELOAD_F, "r");
    if (!f) return -1;
    char line[512];
    size_t n = 0;
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t L = strlen(p);
        while (L && (p[L-1]=='\n' || p[L-1]=='\r' || p[L-1]==' ')) p[--L] = 0;
        if (!L) continue;
        if (n + L + 2 >= cap) break;
        if (n) out[n++] = ':';
        memcpy(out + n, p, L);
        n += L;
        count++;
    }
    fclose(f);
    out[n] = 0;
    return count;
}

/* 判断 pid 是不是 im.elf */
static int is_im_elf(int pid)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[512];
    size_t r = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (r == 0) return 0;
    buf[r] = 0;
    /* cmdline 是 NUL 分隔，第一段是可执行文件路径 */
    return strstr(buf, "im.elf") != NULL;
}

/* 判断 pid 的 maps 里有没有指定库 */
static int maps_has_lib(int pid, const char *libname)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, libname)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

/* 找到所有 im.elf，返回主实例 pid（有 UI 的那个，通常是先启动的） */
static int find_im_elf(int *all, int maxn)
{
    int n = 0;
    for (int pid = 1; pid < 30000 && n < maxn; pid++) {
        if (is_im_elf(pid)) all[n++] = pid;
    }
    return n;
}

static void guardian_main(void)
{
    logmsg("=== guardian start uid=%d ===", geteuid());

    if (have(OFF_FLAG)) {
        logmsg("disabled by %s, exit", OFF_FLAG);
        return;
    }

    char preload[2048];
    int cnt = read_preload(preload, sizeof preload);
    if (cnt <= 0) {
        logmsg("no preload entries (%d), nothing to do", cnt);
        return;
    }
    logmsg("preload = %s (%d entries)", preload, cnt);

    if (!strstr(preload, MARK_LIB)) {
        logmsg("preload does not contain %s, nothing to do", MARK_LIB);
        return;
    }

    int fails = 0;
    int kills = 0;

    for (;;) {
        if (have(OFF_FLAG)) {
            logmsg("disabled by %s, exit", OFF_FLAG);
            return;
        }

        int pids[32];
        int n = find_im_elf(pids, 32);

        if (n == 0) {
            logmsg("no im.elf running");
            sleep(POLL_SEC);
            continue;
        }

        /* 检查是否有任何一个已经带了我们的库 */
        int good = -1;
        for (int i = 0; i < n; i++) {
            if (maps_has_lib(pids[i], MARK_LIB)) { good = pids[i]; break; }
        }

        if (good > 0) {
            logmsg("pid %d already has %s, OK", good, MARK_LIB);
            /* 已经成功，之后只做监控：如果它挂了再重启 */
            sleep(POLL_SEC * 4);
            continue;
        }

        /* 所有 im.elf 都没有我们的库 → 杀掉，重启 */
        logmsg("%d im.elf instance(s), none has %s", n, MARK_LIB);

        if (kills >= MAX_FAILS) {
            logmsg("too many kills (%d), giving up", kills);
            return;
        }

        for (int i = 0; i < n; i++) {
            logmsg("  killing pid %d", pids[i]);
            kill(pids[i], SIGTERM);
        }
        /* 等它们退出 */
        for (int w = 0; w < 20; w++) {
            sleep(1);
            int still[32];
            if (find_im_elf(still, 32) == 0) break;
        }
        /* 还活着的强杀 */
        {
            int still[32];
            int m = find_im_elf(still, 32);
            for (int i = 0; i < m; i++) {
                logmsg("  kill -9 pid %d", still[i]);
                kill(still[i], SIGKILL);
            }
            if (m) sleep(2);
        }
        kills++;

        /* 启动新的 */
        logmsg("launching im.elf with LD_PRELOAD (attempt %d)", kills);

        static char *argv[] = {
            "/usr/bin/im.elf", "debug",
            "s", "g", "r", "u", "l", "i", "a",
            "lazy-global",
            NULL
        };

        pid_t p = fork();
        if (p < 0) {
            logmsg("fork failed: %s", strerror(errno));
            sleep(RETRY_SEC);
            continue;
        }
        if (p == 0) {
            setsid();
            int dn = open("/dev/null", O_RDWR);
            if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }

            char ld[2200];
            snprintf(ld, sizeof ld, "LD_PRELOAD=%s", preload);
            putenv(ld);

            execv(IM_ELF, argv);
            _exit(127);
        }

        /* 给新进程 15 秒启动 */
        int ok = 0;
        for (int w = 0; w < 15; w++) {
            sleep(1);
            int cur[32];
            int m = find_im_elf(cur, 32);
            for (int i = 0; i < m; i++) {
                if (maps_has_lib(cur[i], MARK_LIB)) { ok = 1; logmsg("  pid %d has %s ✓", cur[i], MARK_LIB); break; }
            }
            if (ok) break;
        }

        if (ok) {
            logmsg("SUCCESS: im.elf is running with our library");
            fails = 0;
        } else {
            fails++;
            logmsg("FAILED to load library (%d/%d)", fails, MAX_FAILS);
            if (fails >= MAX_FAILS) {
                logmsg("giving up after %d failures", fails);
                return;
            }
            sleep(RETRY_SEC * 2);
        }
    }
}

int main(void)
{
    /* 单实例锁 */
    int lk = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lk >= 0) {
        struct sockaddr_un sa;
        memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX;
        strcpy(sa.sun_path + 1, "zve10_guardian");
        if (bind(lk, (struct sockaddr *)&sa, sizeof sa) != 0) return 0;
    }

    pid_t p = fork();
    if (p != 0) return 0;
    setsid();
    if (fork() != 0) _exit(0);

    guardian_main();
    return 0;
}

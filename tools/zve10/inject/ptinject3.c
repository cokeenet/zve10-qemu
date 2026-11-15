/* ptinject3 —— 精确选择 UI 线程后注入 dlopen。
 *
 * 修 ptinject2 的问题：im.elf 有 ~200 个线程，PTRACE_ATTACH 选中的线程不确定，
 * 结果寄存器写到了无关线程。现在：
 *   1. 从 /proc/<pid>/task/* 里挑 comm == "im.elf" 的那个 tid
 *   2. 只用 PTRACE_ATTACH 那个 tid
 *   3. 注入前验证该线程 PC 落在已映射的 r-xp 区域
 *   4. 命中 trap 后验证 PC == TRAP_ADDR 且 CPSR T 位正确，才相信 r0
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define TRAP_ADDR   0x9000u
#define DLOPEN_OFF  0xbb0u
#define RTLD_NOW    2
#define R_PC 15
#define R_LR 14
#define R_SP 13
#define R_CPSR 16

struct arm_regs { unsigned long uregs[18]; };

static int find_pid(void)
{
    for (int pid = 1; pid < 4096; pid++) {
        char p[64], buf[128];
        snprintf(p, sizeof p, "/proc/%d/comm", pid);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        int n = read(fd, buf, sizeof buf - 1); close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strncmp(buf, "im.elf", 6) == 0) return pid;
    }
    return -1;
}

/* 挑 comm 与主进程一致的那条线程 */
static int find_ui_tid(int pid)
{
    char dir[64];
    snprintf(dir, sizeof dir, "/proc/%d/task", pid);
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int best = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        int tid = atoi(e->d_name);
        if (tid <= 0) continue;
        if (tid == pid) { best = pid; break; }   /* 主线程就是 UI 线程 */
        char p[96], buf[128];
        snprintf(p, sizeof p, "/proc/%d/task/%d/comm", pid, tid);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        int n = read(fd, buf, sizeof buf - 1); close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strncmp(buf, "im.elf", 6) == 0) { best = tid; break; }
    }
    closedir(d);
    return best;
}

static unsigned long find_libdl(int pid)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    unsigned long best = ~0ul;
    while (fgets(line, sizeof line, f)) {
        unsigned long a;
        if (strstr(line, "libdl-2.11.2.so") && strstr(line, "r-xp") && sscanf(line, "%lx-", &a) == 1)
            if (a < best) best = a;
    }
    fclose(f);
    return best == ~0ul ? 0 : best;
}

/* 该地址是否落在某个 r-xp 映射内 */
static int in_exec_map(int pid, unsigned long addr)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perm) == 3)
            if (addr >= a && addr < b && strchr(perm, 'x')) { ok = 1; break; }
    }
    fclose(f);
    return ok;
}

static int mem_rd(int fd, unsigned long a, void *o, size_t n)
{ return pread(fd, o, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static int mem_wr(int fd, unsigned long a, const void *i, size_t n)
{ return pwrite(fd, i, n, (off_t)a) == (ssize_t)n ? 0 : -1; }

static int count_map(int pid, const char *needle)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int n = 0;
    while (fgets(line, sizeof line, f)) if (strstr(line, needle)) n++;
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    const char *lib = argc > 1 ? argv[1] : "/etc/libnewmenu.so";
    int pid = find_pid();
    if (pid < 0) { printf("FAIL no im.elf\n"); return 1; }
    int tid = find_ui_tid(pid);
    printf("pid=%d ui_tid=%d\n", pid, tid);
    if (tid < 0) { printf("FAIL no ui thread\n"); return 2; }

    unsigned long base = find_libdl(pid);
    printf("libdl=0x%08lX dlopen=0x%08lX\n", base, base ? base + DLOPEN_OFF : 0);
    if (!base) return 3;

    printf("maps_before=%d\n", count_map(pid, "libnewmenu"));

    if (ptrace(PTRACE_ATTACH, tid, 0, 0) < 0) { printf("FAIL attach tid %d\n", errno); return 4; }
    int st = 0;
    if (waitpid(tid, &st, 0) < 0 || !WIFSTOPPED(st)) { printf("FAIL wait\n"); return 5; }

    struct arm_regs regs, saved;
    if (ptrace(PTRACE_GETREGS, tid, 0, &regs) < 0) { printf("FAIL getregs\n"); return 6; }
    saved = regs;
    printf("ui thread PC=0x%08lX SP=0x%08lX CPSR=0x%08lX\n",
           regs.uregs[R_PC], regs.uregs[R_SP], regs.uregs[R_CPSR]);
    if (!in_exec_map(pid, regs.uregs[R_PC])) {
        printf("FAIL ui PC not in an x map\n"); ptrace(PTRACE_DETACH,tid,0,0); return 7;
    }

    char mp[64]; snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { printf("FAIL mem %d\n", errno); ptrace(PTRACE_DETACH,tid,0,0); return 8; }

    /* 用该线程自己的栈下方作字符串区 */
    /* 关键：dlopen 会用自己的栈帧（向下增长），SP-0x200 会被它踩掉。
     * 实测 dlerror 报 "/etc<?>db: cannot open" —— 路径被覆盖。
     * 放到 SP-0x8000，远离栈帧又仍在栈映射内（实测栈有 132 KB 余量）。 */
    unsigned long sb = (regs.uregs[R_SP] - 0x8000) & ~3ul;
    size_t sl = strlen(lib) + 1;
    if (mem_wr(fd, sb, lib, sl) < 0) { printf("FAIL str\n"); goto done; }
    char ck[64]; memset(ck,0,sizeof ck);
    if (mem_rd(fd, sb, ck, sl) < 0 || strcmp(ck, lib)) { printf("FAIL str rb\n"); goto done; }
    printf("strbuf=0x%08lX ok\n", sb);

    unsigned char orig[4];
    if (mem_rd(fd, TRAP_ADDR, orig, 4) < 0) { printf("FAIL rd trap\n"); goto done; }
    unsigned char bkpt[4] = {0x70,0x00,0x20,0xE1};
    if (mem_wr(fd, TRAP_ADDR, bkpt, 4) < 0) { printf("FAIL wr bkpt\n"); goto done; }

    regs.uregs[0] = sb;
    regs.uregs[1] = RTLD_NOW;
    regs.uregs[R_LR] = TRAP_ADDR;
    regs.uregs[R_PC] = base + DLOPEN_OFF;
    regs.uregs[R_CPSR] |= (1ul << 5);
    if (ptrace(PTRACE_SETREGS, tid, 0, &regs) < 0) { printf("FAIL setregs\n"); goto restore; }

    if (ptrace(PTRACE_CONT, tid, 0, 0) < 0) { printf("FAIL cont\n"); goto restore; }
    st = 0;
    if (waitpid(tid, &st, 0) < 0) { printf("FAIL wait2\n"); goto restore; }
    if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGTRAP) {
        printf("FAIL stop stopped=%d sig=%d\n", WIFSTOPPED(st), WSTOPSIG(st)); goto restore;
    }

    struct arm_regs after;
    if (ptrace(PTRACE_GETREGS, tid, 0, &after) < 0) { printf("FAIL getregs2\n"); goto restore; }
    printf("trap: PC=0x%08lX LR=0x%08lX CPSR=0x%08lX r0=0x%08lX\n",
           after.uregs[R_PC], after.uregs[R_LR], after.uregs[R_CPSR], after.uregs[0]);
    /* 唯一正确判据：PC 停在我们的陷阱上。T 位与进入时的模式一致，不作要求。 */
    int trustworthy = (after.uregs[R_PC] == TRAP_ADDR);
    printf("stop_is_ours=%d\n", trustworthy);

    mem_wr(fd, TRAP_ADDR, orig, 4);
    ptrace(PTRACE_SETREGS, tid, 0, &saved);
    close(fd);
    if (ptrace(PTRACE_DETACH, tid, 0, 0) < 0) { printf("FAIL detach\n"); return 9; }

    int n = count_map(pid, "libnewmenu");
    printf("maps_after=%d\n", n);
    printf("VERDICT %s\n", n > 0 ? "LIBRARY_MAPPED" : "NOT_MAPPED");
    return trustworthy && n > 0 ? 0 : 20;

restore:
    mem_wr(fd, TRAP_ADDR, orig, 4);
    ptrace(PTRACE_SETREGS, tid, 0, &saved);
done:
    close(fd);
    ptrace(PTRACE_DETACH, tid, 0, 0);
    return 8;
}

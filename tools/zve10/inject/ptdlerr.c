/* dlopen 失败后调 dlerror() 把原因读回来 —— 判定为什么加载失败。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define TRAP_ADDR    0x9000u
#define DLOPEN_OFF   0xbb0u
#define DLERROR_OFF  0xdecu    /* libdl-2.11.2.so dlerror, Thumb */
#define RTLD_NOW     2
#define R_PC 15
#define R_LR 14
#define R_SP 13
#define R_CPSR 16

struct arm_regs { unsigned long uregs[18]; };

static int find_pid(void)
{
    for (int pid = 1; pid < 4096; pid++) {
        char p[64], buf[96];
        snprintf(p, sizeof p, "/proc/%d/comm", pid);
        int fd = open(p, O_RDONLY); if (fd < 0) continue;
        int n = read(fd, buf, sizeof buf - 1); close(fd);
        if (n <= 0) continue; buf[n] = 0;
        if (strncmp(buf, "im.elf", 6) == 0) return pid;
    }
    return -1;
}
static unsigned long find_sym_base(int pid, const char *name, const char *perm)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r"); if (!f) return 0;
    unsigned long best = ~0ul;
    while (fgets(line, sizeof line, f)) {
        unsigned long a;
        if (strstr(line, name) && strstr(line, perm) && sscanf(line, "%lx-", &a) == 1)
            if (a < best) best = a;
    }
    fclose(f);
    return best == ~0ul ? 0 : best;
}
static int mem_rd(int fd, unsigned long a, void *o, size_t n)
{ return pread(fd, o, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static int mem_wr(int fd, unsigned long a, const void *i, size_t n)
{ return pwrite(fd, i, n, (off_t)a) == (ssize_t)n ? 0 : -1; }

/* 在目标进程里调用一个无参函数，用一个 4 字节陷阱抓返回 */
static int call_noarg(int tid, int fd, unsigned long fn, unsigned long *ret)
{
    struct arm_regs r, saved;
    if (ptrace(PTRACE_GETREGS, tid, 0, &r) < 0) return -1;
    saved = r;
    unsigned char orig[4];
    if (mem_rd(fd, TRAP_ADDR, orig, 4) < 0) return -2;
    unsigned char bkpt[4] = {0x70,0x00,0x20,0xE1};
    if (mem_wr(fd, TRAP_ADDR, bkpt, 4) < 0) return -3;
    r.uregs[R_LR] = TRAP_ADDR;
    r.uregs[R_PC] = fn;
    r.uregs[R_CPSR] |= (1ul << 5);
    if (ptrace(PTRACE_SETREGS, tid, 0, &r) < 0) { mem_wr(fd,TRAP_ADDR,orig,4); return -4; }
    if (ptrace(PTRACE_CONT, tid, 0, 0) < 0) { mem_wr(fd,TRAP_ADDR,orig,4); return -5; }
    int st = 0;
    if (waitpid(tid, &st, 0) < 0) { mem_wr(fd,TRAP_ADDR,orig,4); return -6; }
    struct arm_regs after;
    ptrace(PTRACE_GETREGS, tid, 0, &after);
    *ret = after.uregs[0];
    mem_wr(fd, TRAP_ADDR, orig, 4);
    ptrace(PTRACE_SETREGS, tid, 0, &saved);
    return 0;
}

int main(int argc, char **argv)
{
    const char *lib = argc > 1 ? argv[1] : "/etc/libnewmenu.so";
    const char *dlbase_sym = argc > 2 ? argv[2] : "libdl-2.11.2.so";
    unsigned long err_off = argc > 3 ? strtoul(argv[3], 0, 0) : DLERROR_OFF;

    int pid = find_pid();
    if (pid < 0) { printf("FAIL no im.elf\n"); return 1; }
    unsigned long base = find_sym_base(pid, dlbase_sym, "r-xp");
    if (!base) { printf("FAIL no %s\n", dlbase_sym); return 2; }
    printf("pid=%d base(%s)=0x%08lX dlerror=0x%08lX\n", pid, dlbase_sym, base, base + err_off);

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { printf("FAIL attach\n"); return 3; }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 4;
    char mp[64]; snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { printf("FAIL mem\n"); ptrace(PTRACE_DETACH,pid,0,0); return 5; }

    unsigned long ret = 0;
    if (call_noarg(pid, fd, base + err_off, &ret) == 0) {
        printf("dlerror() = 0x%08lX\n", ret);
        if (ret) {
            char buf[256]; memset(buf,0,sizeof buf);
            if (mem_rd(fd, ret, buf, sizeof buf - 1) == 0) printf("message: %s\n", buf);
        }
    } else printf("dlerror call failed\n");

    close(fd);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 0;
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define IM_BASE      0x8000u
#define DLOPEN_PLT   0x9108u
#define FREE_PLT     0x9000u
#define RTLD_NOW     2
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
        int n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strncmp(buf, "im.elf", 6) == 0) return pid;
    }
    return -1;
}
static int mem_read(int fd, unsigned long a, void *o, size_t n)
{ return pread(fd, o, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static int mem_write(int fd, unsigned long a, const void *i, size_t n)
{ return pwrite(fd, i, n, (off_t)a) == (ssize_t)n ? 0 : -1; }

int main(int argc, char **argv)
{
    const char *lib = argc > 1 ? argv[1] : "/etc/libnewmenu.so";
    int pid = find_pid();
    if (pid < 0) { printf("FAIL: im.elf not found\n"); return 1; }
    printf("im.elf pid=%d target=%s\n", pid, lib);

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { printf("FAIL attach %d\n", errno); return 2; }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0 || !WIFSTOPPED(st)) { printf("FAIL wait\n"); return 3; }
    printf("attached sig=%d\n", WSTOPSIG(st));

    struct arm_regs regs, saved;
    if (ptrace(PTRACE_GETREGS, pid, 0, &regs) < 0) { printf("FAIL getregs\n"); return 4; }
    saved = regs;
    printf("saved PC=0x%08lX SP=0x%08lX LR=0x%08lX\n",
           regs.uregs[R_PC], regs.uregs[R_SP], regs.uregs[R_LR]);

    char mp[64];
    snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { printf("FAIL open mem %d\n", errno); return 5; }

    unsigned long strbuf = (regs.uregs[R_SP] - 0x400) & ~3ul;
    size_t slen = strlen(lib) + 1;
    if (mem_write(fd, strbuf, lib, slen) < 0) { printf("FAIL write str\n"); return 6; }
    char chk[64]; memset(chk,0,sizeof chk);
    mem_read(fd, strbuf, chk, slen);
    printf("path @0x%08lX = %s\n", strbuf, chk);
    if (strcmp(chk, lib) != 0) { printf("FAIL str readback\n"); return 7; }

    unsigned long trap = IM_BASE + (FREE_PLT - IM_BASE);
    unsigned char orig[4];
    if (mem_read(fd, trap, orig, 4) < 0) { printf("FAIL read trap\n"); return 8; }
    unsigned char bkpt[4] = { 0x70, 0x00, 0x20, 0xE1 };
    if (mem_write(fd, trap, bkpt, 4) < 0) { printf("FAIL write bkpt\n"); return 9; }
    printf("BKPT at 0x%08lX (orig %02x%02x%02x%02x)\n", trap, orig[0],orig[1],orig[2],orig[3]);

    regs.uregs[0] = strbuf;
    regs.uregs[1] = RTLD_NOW;
    regs.uregs[R_LR] = trap;
    regs.uregs[R_PC] = DLOPEN_PLT;
    regs.uregs[R_CPSR] &= ~(1ul << 5);
    if (ptrace(PTRACE_SETREGS, pid, 0, &regs) < 0) { printf("FAIL setregs\n"); return 10; }
    printf("jump dlopen@plt=0x%08X\n", DLOPEN_PLT);

    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) { printf("FAIL cont\n"); return 11; }
    st = 0;
    if (waitpid(pid, &st, 0) < 0) { printf("FAIL wait2\n"); return 12; }
    printf("stopped WIFSTOPPED=%d sig=%d\n", WIFSTOPPED(st), WSTOPSIG(st));

    struct arm_regs after;
    ptrace(PTRACE_GETREGS, pid, 0, &after);
    printf("dlopen r0=0x%08lX %s\n", after.uregs[0],
           after.uregs[0] ? "HANDLE OK" : "NULL(failed)");

    mem_write(fd, trap, orig, 4);
    ptrace(PTRACE_SETREGS, pid, 0, &saved);
    close(fd);
    if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0) printf("FAIL detach\n");
    else printf("restored+detached, resumed unchanged\n");
    return after.uregs[0] ? 0 : 20;
}

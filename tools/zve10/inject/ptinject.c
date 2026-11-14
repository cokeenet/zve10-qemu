/* ptinject —— 让运行中的 im.elf 调用 dlopen("/etc/libnewmenu.so")。
 *
 * 只改运行内存，不写磁盘：路径写到目标自己的栈，BKPT 打在未使用的 free@plt，
 * 设好 r0/r1/lr/pc 后放行，收到 SIGTRAP 后读回返回值，再还原全部寄存器与字节。
 *
 * 用法:
 *   ptinject.elf [lib] [--once]      注入一次并报告
 *   ptinject.elf [lib] --repeat N    连续注入 N 次（每次返回后隔 1 秒）
 *   ptinject.elf --check             只读自检：验证 PLT 桩字节与地址推算
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define IM_BASE      0x8000u    /* im.elf 的 p_vaddr 基址 */
#define DLOPEN_PLT   0x9108u    /* ARM PLT 桩：add ip,pc,#0; ldr pc,[ip,#0xcc]! */
#define TRAP_PLT     0x9000u    /* free@plt：全 .text 零调用点，桩体是 bx lr; nop */
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

/* 库加载后它会出现在目标进程的 maps 里；这是注入是否真的成功的独立证据。 */
static int mapped_count(int pid, const char *needle)
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

/* 注入一次。返回 0 成功（dlopen 返回非 NULL），非 0 失败。 */
static int inject_once(const char *lib)
{
    int pid = find_pid();
    if (pid < 0) { printf("FAIL no im.elf\n"); return 1; }

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { printf("FAIL attach errno=%d\n", errno); return 2; }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0 || !WIFSTOPPED(st)) { printf("FAIL wait\n"); return 3; }

    struct arm_regs regs, saved;
    if (ptrace(PTRACE_GETREGS, pid, 0, &regs) < 0) { printf("FAIL getregs\n"); return 4; }
    saved = regs;

    char mp[64];
    snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { printf("FAIL mem errno=%d\n", errno); ptrace(PTRACE_DETACH, pid,0,0); return 5; }

    /* 1) 路径写到当前栈顶下方（ABI 保证 SP 以下是空闲区） */
    unsigned long strbuf = (regs.uregs[R_SP] - 0x400) & ~3ul;
    size_t slen = strlen(lib) + 1;
    if (mem_write(fd, strbuf, lib, slen) < 0) { printf("FAIL str write\n"); goto fail5; }
    char chk[64]; memset(chk, 0, sizeof chk);
    if (mem_read(fd, strbuf, chk, slen) < 0 || strcmp(chk, lib) != 0) {
        printf("FAIL str readback\n"); goto fail5;
    }

    /* 2) 在未使用的 PLT 桩上打 BKPT */
    unsigned long trap = TRAP_PLT;
    unsigned char orig[4];
    if (mem_read(fd, trap, orig, 4) < 0) { printf("FAIL trap read\n"); goto fail5; }
    unsigned char bkpt[4] = { 0x70, 0x00, 0x20, 0xE1 };   /* BKPT #0 */
    if (mem_write(fd, trap, bkpt, 4) < 0) { printf("FAIL trap write\n"); goto fail5; }

    /* 3) r0=path r1=RTLD_NOW lr=trap pc=dlopen@plt，清 Thumb 位走 ARM */
    regs.uregs[0] = strbuf;
    regs.uregs[1] = RTLD_NOW;
    regs.uregs[R_LR] = trap;
    regs.uregs[R_PC] = DLOPEN_PLT;
    regs.uregs[R_CPSR] &= ~(1ul << 5);
    if (ptrace(PTRACE_SETREGS, pid, 0, &regs) < 0) { printf("FAIL setregs\n"); goto restore; }

    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) { printf("FAIL cont\n"); goto restore; }
    st = 0;
    if (waitpid(pid, &st, 0) < 0) { printf("FAIL wait2\n"); goto restore; }
    if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGTRAP) {
        printf("FAIL not-intended-stop stopped=%d sig=%d\n", WIFSTOPPED(st), WSTOPSIG(st));
        goto restore;
    }

    struct arm_regs after;
    if (ptrace(PTRACE_GETREGS, pid, 0, &after) < 0) { printf("FAIL getregs2\n"); goto restore; }
    unsigned long handle = after.uregs[0];
    printf("dlopen r0=0x%08lX %s\n", handle, handle ? "HANDLE_OK" : "NULL_FAILED");

    mem_write(fd, trap, orig, 4);                 /* 还原 PLT 字节 */
    ptrace(PTRACE_SETREGS, pid, 0, &saved);       /* 还原全部寄存器 */
    close(fd);
    if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0) { printf("FAIL detach\n"); return 9; }
    return handle ? 0 : 20;

restore:
    mem_write(fd, trap, orig, 4);
    ptrace(PTRACE_SETREGS, pid, 0, &saved);
fail5:
    close(fd);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 8;
}

/* 只读自检：不碰相机，验证常量本身就是对的。 */
static int self_check(void)
{
    printf("IM_BASE    = 0x%08X\n", IM_BASE);
    printf("TRAP_PLT   = 0x%08X  (free@plt, 期望桩体 78 47 c0 46 = bx lr; nop)\n", TRAP_PLT);
    printf("DLOPEN_PLT = 0x%08X  (dlopen@plt, ARM 桩)\n", DLOPEN_PLT);
    printf("BKPT 编码  = 70 00 20 E1 (ARM BKPT #0)\n");
    printf("栈偏移     = SP-0x400（ABI: SP 以下为该线程空闲栈）\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *lib = "/etc/libnewmenu.so";
    int repeat = 1;
    if (argc > 1 && strcmp(argv[1], "--check") == 0) return self_check();
    if (argc > 1 && argv[1][0] != '-') lib = argv[1];
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) repeat = atoi(argv[i + 1]);
    if (repeat < 1) repeat = 1;
    if (repeat > 20) repeat = 20;

    int pid = find_pid();
    if (pid < 0) { printf("FAIL no im.elf\n"); return 1; }
    printf("im.elf pid=%d lib=%s repeat=%d\n", pid, lib, repeat);
    int before = mapped_count(pid, "libnewmenu");
    printf("maps before: libnewmenu x%d\n", before);

    int rc = 0;
    for (int i = 0; i < repeat; i++) {
        printf("--- attempt %d/%d ---\n", i + 1, repeat);
        rc = inject_once(lib);
        if (rc == 0) break;                 /* dlopen 成功就停 */
        if (rc == 1 || rc == 2) break;      /* 进程没了 / attach 不了，别硬来 */
        sleep(1);
    }

    int after = mapped_count(pid, "libnewmenu");
    printf("maps after : libnewmenu x%d\n", after);
    printf("VERDICT %s\n", after > before ? "LIBRARY_MAPPED" :
                          (rc == 0 ? "HANDLE_BUT_NOT_MAPPED" : "NOT_MAPPED"));
    return rc;
}

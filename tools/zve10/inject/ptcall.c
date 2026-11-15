/* ptcall —— 在目标进程里调用任意【已映射库】的导出函数（最多 2 个整型参数）。
 *
 * 用法: ptcall <libpath-substr> <symbol> [arg0] [arg1]
 *
 * 库基址从 /proc/<pid>/maps 取；符号偏移从本地同名 .so 的 .dynsym 取。
 * 用 BKPT 陷阱抓返回，读完 r0 再还原全部寄存器与字节。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <elf.h>

#define TRAP_ADDR 0x9000u
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
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        int n = read(fd, buf, sizeof buf - 1); close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strncmp(buf, "im.elf", 6) == 0) return pid;
    }
    return -1;
}

static unsigned long lib_base(int pid, const char *needle)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    unsigned long best = ~0ul;
    while (fgets(line, sizeof line, f)) {
        unsigned long a;
        if (strstr(line, needle) && strstr(line, "r-xp") && sscanf(line, "%lx-", &a) == 1)
            if (a < best) best = a;
    }
    fclose(f);
    return best == ~0ul ? 0 : best;
}

/* 从本地 ELF 的 .dynsym 取符号的 st_value（即相对基址的偏移，Thumb 位保留） */
static unsigned long sym_off(const char *elfpath, const char *name)
{
    int fd = open(elfpath, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "open %s: %d\n", elfpath, errno); return 0; }
    struct stat st;
    fstat(fd, &st);
    unsigned char *d = malloc((size_t)st.st_size);
    if (!d) { close(fd); return 0; }
    pread(fd, d, (size_t)st.st_size, 0);
    close(fd);

    Elf32_Ehdr *eh = (Elf32_Ehdr *)d;
    if (memcmp(eh->e_ident, ELFMAG, 4) || eh->e_ident[EI_CLASS] != ELFCLASS32) { free(d); return 0; }

    Elf32_Shdr *sh = (Elf32_Shdr *)(d + eh->e_shoff);
    unsigned long result = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_DYNSYM) continue;
        Elf32_Sym *sym = (Elf32_Sym *)(d + sh[i].sh_offset);
        int n = (int)(sh[i].sh_size / sizeof(Elf32_Sym));
        const char *str = (const char *)(d + sh[sh[i].sh_link].sh_offset);
        for (int k = 1; k < n; k++)
            if (strcmp(str + sym[k].st_name, name) == 0) { result = sym[k].st_value; break; }
        break;
    }
    free(d);
    return result;
}

static int mem_rd(int fd, unsigned long a, void *o, size_t n)
{ return pread(fd, o, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static int mem_wr(int fd, unsigned long a, const void *i, size_t n)
{ return pwrite(fd, i, n, (off_t)a) == (ssize_t)n ? 0 : -1; }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "用法: %s <libpath-substr> <local.so> <symbol> [a0] [a1]\n", argv[0]); return 2; }
    const char *libneedle = argv[1];
    const char *localso   = argv[2];
    const char *symname   = argv[3];
    unsigned long a0 = argc > 4 ? strtoul(argv[4], 0, 0) : 0;
    unsigned long a1 = argc > 5 ? strtoul(argv[5], 0, 0) : 0;

    int pid = find_pid();
    if (pid < 0) { printf("FAIL no im.elf\n"); return 1; }
    unsigned long base = lib_base(pid, libneedle);
    unsigned long off  = sym_off(localso, symname);
    printf("pid=%d base=0x%08lX symoff=0x%08lX fn=0x%08lX\n", pid, base, off, base ? base + off : 0);
    if (!base || !off) { printf("FAIL resolve\n"); return 3; }

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { printf("FAIL attach %d\n", errno); return 4; }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 5;

    struct arm_regs r, saved;
    if (ptrace(PTRACE_GETREGS, pid, 0, &r) < 0) { printf("FAIL getregs\n"); return 6; }
    saved = r;

    char mp[64]; snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { printf("FAIL mem %d\n", errno); ptrace(PTRACE_DETACH,pid,0,0); return 7; }

    unsigned char orig[4];
    if (mem_rd(fd, TRAP_ADDR, orig, 4) < 0) { printf("FAIL rd trap\n"); close(fd); ptrace(PTRACE_DETACH,pid,0,0); return 8; }
    unsigned char bkpt[4] = {0x70,0x00,0x20,0xE1};
    if (mem_wr(fd, TRAP_ADDR, bkpt, 4) < 0) { printf("FAIL wr bkpt\n"); close(fd); ptrace(PTRACE_DETACH,pid,0,0); return 9; }

    r.uregs[0] = a0;
    r.uregs[1] = a1;
    r.uregs[R_LR] = TRAP_ADDR;
    r.uregs[R_PC] = base + off;
    r.uregs[R_CPSR] |= (1ul << 5);
    if (ptrace(PTRACE_SETREGS, pid, 0, &r) < 0) { printf("FAIL setregs\n"); mem_wr(fd,TRAP_ADDR,orig,4); close(fd); ptrace(PTRACE_DETACH,pid,0,0); return 10; }

    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) { printf("FAIL cont\n"); mem_wr(fd,TRAP_ADDR,orig,4); close(fd); ptrace(PTRACE_DETACH,pid,0,0); return 11; }
    st = 0;
    if (waitpid(pid, &st, 0) < 0) { printf("FAIL wait2\n"); mem_wr(fd,TRAP_ADDR,orig,4); close(fd); ptrace(PTRACE_DETACH,pid,0,0); return 12; }

    struct arm_regs after;
    ptrace(PTRACE_GETREGS, pid, 0, &after);
    printf("stop PC=0x%08lX r0=0x%08lX (dec %ld)\n",
           after.uregs[R_PC], after.uregs[0], (long)after.uregs[0]);
    printf("stop_is_ours=%d\n", after.uregs[R_PC] == TRAP_ADDR);

    mem_wr(fd, TRAP_ADDR, orig, 4);
    ptrace(PTRACE_SETREGS, pid, 0, &saved);
    close(fd);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 0;
}

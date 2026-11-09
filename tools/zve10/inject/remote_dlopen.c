/* remote_dlopen.c — 用 ptrace 在运行中的 im.elf 里调用 dlopen
 *
 * 原理：
 *   ① PTRACE_ATTACH 停住目标
 *   ② 保存所有寄存器（GETREGS）
 *   ③ 在目标进程栈上写一段 shellcode（或直接改 PC 调函数）
 *   ④ 让 PC 指向目标函数，设好参数寄存器
 *   ⑤ 设 LR 指向一个"陷阱地址"（如 0 或者已知的 breakpoint）
 *   ⑥ PTRACE_CONT，等它 SIGSEGV
 *   ⑦ 读 r0 = 返回值
 *   ⑧ 恢复所有寄存器，DETACH
 *
 * 更简单可靠的做法（stub 注入）：
 *   ① 在目标进程栈上写一段 ARM stub：
 *        push {r4-r12, lr}
 *        ldr r0, =path_addr
 *        mov r1, #2          (RTLD_NOW)
 *        movw/movt r12, =dlopen_addr
 *        blx r12
 *        pop {r4-r12, lr}
 *        movw/movt r12, =TRAP
 *        bkpt #0  /  or  mov pc, lr with r0 preserved
 *   ② 把 PC 指向这段 stub
 *   ③ PTRACE_CONT
 *   ④ 等 SIGSEGV / SIGTRAP
 *   ⑤ 读 r0
 *   ⑥ 恢复
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <dlfcn.h>

/* ARM reg indices removed - using struct user_regs fields */
#define _ARM_R0_DUMMY 0

static int write_mem(int pid, unsigned long addr, const void *buf, size_t len) {
    char p[64]; snprintf(p, sizeof p, "/proc/%d/mem", pid);
    int fd = open(p, O_RDWR);
    if (fd < 0) return -1;
    ssize_t n = pwrite(fd, buf, len, (off_t)addr);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

static int read_mem(int pid, unsigned long addr, void *buf, size_t len) {
    char p[64]; snprintf(p, sizeof p, "/proc/%d/mem", pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = pread(fd, buf, len, (off_t)addr);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

/* 找目标进程里可写的匿名内存段（栈或 heap），返回起始地址和大小 */
static unsigned long find_writable(int pid, unsigned long *size) {
    char p[64]; snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512];
    unsigned long best = 0, bestsz = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b;
        char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perm) != 3) continue;
        if (perm[0] != 'r' || perm[1] != 'w') continue;
        /* 找 stack 或 [heap] */
        if (strstr(line, "[stack]") || strstr(line, "[heap]")) {
            unsigned long sz = b - a;
            if (sz > bestsz) { best = a; bestsz = sz; }
        }
    }
    fclose(f);
    if (size) *size = bestsz;
    return best;
}

/* 在本地 libdl 里找 dlopen 的偏移（相对 libdl 基址） */
static unsigned long local_dlopen_off(void) {
    /* 相机 libdl-2.11.2.so 里 dlopen 相对基址 = 0xbb0（离线 nm 得到） */
    return 0xbb0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("用法: %s <pid> <so路径> [符号名]\n", argv[0]);
        printf("  例: %s 157 /etc/libnewmenu.so\n", argv[0]);
        return 1;
    }
    int pid = atoi(argv[1]);
    const char *sopath = argv[2];

    printf("[*] 目标 pid=%d, so=%s\n", pid, sopath);

    unsigned long dlopen_off = local_dlopen_off();
    printf("[*] 本地 dlopen 相对 libdl 基址的偏移 = 0x%lx\n", dlopen_off);
    if (!dlopen_off) { printf("[-] 找不到 dlopen 偏移\n"); return 1; }

    /* 找目标里 libdl 的基址 */
    char p[64]; snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) { perror("maps"); return 1; }
    unsigned long tbase = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "libdl-2.11.2.so") && strstr(line, "r-xp")) {
            sscanf(line, "%lx-", &tbase); break;
        }
    }
    fclose(f);
    if (!tbase) { printf("[-] 目标里找不到 libdl\n"); return 1; }
    unsigned long dlopen_addr = tbase + dlopen_off;
    printf("[*] 目标 libdl 基址 = 0x%lx，dlopen = 0x%lx\n", tbase, dlopen_addr);

    /* 找可写区域 */
    unsigned long wsz = 0;
    unsigned long wbase = find_writable(pid, &wsz);
    if (!wbase) { printf("[-] 找不到可写区\n"); return 1; }
    printf("[*] 可写区 = 0x%lx (%lu KB)\n", wbase, wsz/1024);

    /* 选一块靠末尾的空间放 path + stub */
    unsigned long spot = wbase + wsz - 0x800;
    spot &= ~0xFUL;
    unsigned long path_addr = spot;
    unsigned long stub_addr = (spot + 0x100) & ~0xFUL;
    unsigned long saved_addr = (spot + 0x300) & ~0xFUL;

    printf("[*] path@0x%lx  stub@0x%lx  saved@0x%lx\n", path_addr, stub_addr, saved_addr);

    

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { perror("attach"); return 1; }
    int st;
    waitpid(pid, &st, 0);

    struct user_regs uregs;
    if (ptrace(PTRACE_GETREGS, pid, 0, &uregs) < 0) { perror("getregs"); ptrace(PTRACE_DETACH,pid,0,0); return 1; }

    /* 保存原寄存器（写到目标内存） */
    write_mem(pid, saved_addr, &uregs, sizeof uregs);

    /* 写路径字符串 */
    write_mem(pid, path_addr, sopath, strlen(sopath)+1);

    /* 构造 ARM stub（Thumb-2 或者纯 ARM？im.elf 是 ARM 模式）
     * 用 ARM 指令，简单可靠：
     *   push {r0-r3, r12, lr}
     *   movw r0, #path_lo ; movt r0, #path_hi
     *   mov  r1, #2                    (RTLD_NOW)
     *   movw r12, #dl_lo ; movt r12, #dl_hi
     *   blx  r12
     *   pop  {r0-r3, r12, lr}
     *   mov  pc, lr          ← 返回，但我们要停下来，所以用 bkpt
     */
    unsigned int stub[32]; int n = 0;
    stub[n++] = 0xE92D500F;             /* push {r0-r3, r12, lr} */
    /* movw r0, #lo16 */
    unsigned lo = path_addr & 0xFFFF, hi = (path_addr >> 16) & 0xFFFF;
    stub[n++] = 0xE3000000 | ((lo & 0xF000) << 4) | (lo & 0x0FFF);            /* movw r0, lo */
    stub[n++] = 0xE3400000 | ((hi & 0xF000) << 4) | (hi & 0x0FFF);            /* movt r0, hi */
    stub[n++] = 0xE3A01002;             /* mov r1, #2 */
    unsigned dlo = dlopen_addr & 0xFFFF, dhi = (dlopen_addr >> 16) & 0xFFFF;
    stub[n++] = 0xE30CC000 | ((dlo & 0xF000) << 4) | (dlo & 0x0FFF);          /* movw r12, lo */
    stub[n++] = 0xE34CC000 | ((dhi & 0xF000) << 4) | (dhi & 0x0FFF);          /* movt r12, hi */
    stub[n++] = 0xE12FFF3C;             /* blx r12 */
    stub[n++] = 0xE8BD500F;             /* pop {r0-r3, r12, lr} */
    stub[n++] = 0xE1200070;             /* bkpt #0  → SIGTRAP */

    write_mem(pid, stub_addr, stub, n * 4);
    printf("[*] stub 写入 %d 条指令\n", n);

    /* 改 PC 到 stub，SP 下移一点给它用 */
    uregs.uregs[15] = stub_addr;
    uregs.uregs[13] = (stub_addr - 0x100) & ~0xFUL;
    if (ptrace(PTRACE_SETREGS, pid, 0, &uregs) < 0) { perror("setregs"); ptrace(PTRACE_DETACH,pid,0,0); return 1; }

    printf("[*] 继续执行...\n");
    ptrace(PTRACE_CONT, pid, 0, 0);
    waitpid(pid, &st, 0);

    if (WIFSTOPPED(st)) printf("[*] 停止信号: %d\n", WSTOPSIG(st));

    struct user_regs after;
    if (ptrace(PTRACE_GETREGS, pid, 0, &after) == 0) {
        printf("[+] dlopen 返回 = 0x%lx\n", (unsigned long)after.uregs[0]);
        if (!after.uregs[0]) printf("[-] 失败（libdl 返回 NULL）\n");
        else printf("[+] 成功！\n");
    }

    /* 恢复寄存器 */
    struct user_regs saved;
    read_mem(pid, saved_addr, &saved, sizeof saved);
    ptrace(PTRACE_SETREGS, pid, 0, &saved);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    printf("[*] 已恢复并 detach\n");
    return 0;
}

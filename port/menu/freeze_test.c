/* 最小验证：能否 ptrace 冻结 im.elf 的全部线程并干净恢复。
 * 纯 syscall，无 libc 依赖。只做 ATTACH/DETACH，不写任何内存。
 */
#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_getdents64 217
#define SYS_ptrace    190
#define SYS_wait4     114
#define SYS_getpid     20
#define SYS_nanosleep  162

#define PTRACE_ATTACH  16
#define PTRACE_DETACH  17
#define __WALL         0x40000000

static inline int sc0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory");return r0;}
static inline int sc1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory");return r0;}
static inline int sc2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory");return r0;}
static inline int sc3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory");return r0;}
static inline int sc4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory");return r0;}

static char buf[8192];
static int out;
static void puts_(const char *s){ int n=0; while(s[n])n++; sc3(SYS_write,out,(int)s,n); }
static void putd(long v){
    char b[32]; int n=0;
    if (v < 0) { puts_("-"); v = -v; }
    if (v == 0) { puts_("0"); return; }
    while (v > 0) {
        /* 商 = 不断减 10 的次数；余 = 最后剩下的 */
        long q = 0, r = v;
        while (r >= 10) { r -= 10; q++; }
        b[n++] = (char)('0' + (int)r);
        v = q;
    }
    while (n--) {
        char c = b[n];
        sc3(SYS_write, out, (int)&c, 1);
    }
}

struct dirent64 { unsigned long long d_ino; long long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };

int main(void)
{
    out = sc1(SYS_open, (int)"/log/freeze-test.log");
    if (out < 0) { out = 1; puts_("cannot open log\n"); return 1; }

    int fd = sc2(SYS_open, (int)"/proc/157/task", 0);
    if (fd < 0) { puts_("no /proc/157/task\n"); return 2; }

    int tids[512]; int n = 0;
    for (;;) {
        long got = sc3(SYS_getdents64, fd, (int)buf, sizeof buf);
        if (got <= 0) break;
        long off = 0;
        while (off < got) {
            struct dirent64 *d = (struct dirent64 *)(buf + off);
            int is_num = 1; int i = 0;
            if (!d->d_name[0]) { off += d->d_reclen; continue; }
            while (d->d_name[i]) { if (d->d_name[i] < '0' || d->d_name[i] > '9') { is_num = 0; break; } i++; }
            if (is_num && i > 0 && n < 512) {
                int v = 0, j;
                for (j = 0; j < i; j++) v = v * 10 + (d->d_name[j] - '0');
                tids[n++] = v;
            }
            off += d->d_reclen;
        }
    }
    sc1(SYS_close, fd);
    puts_("threads found = "); putd(n); puts_("\n");

    int attached = 0, failed = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (sc4(SYS_ptrace, PTRACE_ATTACH, 0, tids[i], 0) == 0) {
            int st = 0;
            sc4(SYS_wait4, tids[i], (int)&st, __WALL, 0);
            attached++;
        } else failed++;
    }
    puts_("attached="); putd(attached); puts_(" failed="); putd(failed); puts_("\n");

    /* 立刻全部 detach —— 不写任何内存 */
    int detached = 0;
    for (i = 0; i < n; i++) {
        if (sc4(SYS_ptrace, PTRACE_DETACH, 0, tids[i], 0) == 0) detached++;
    }
    puts_("detached="); putd(detached); puts_("\n");
    puts_(attached == n ? "FREEZE_TEST_OK\n" : "FREEZE_TEST_PARTIAL\n");
    return 0;
}

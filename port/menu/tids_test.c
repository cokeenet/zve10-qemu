/* 验证 ptrace 冻结能力。TID 由命令行参数传入（shell 展开 /proc/PID/task/*）。
 * 只 ATTACH + DETACH，不写任何内存。 */
#define SYS_exit        1
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_ptrace    190
#define SYS_wait4     114
#define PTRACE_ATTACH  16
#define PTRACE_DETACH  17
#define __WALL         0x40000000

static inline int sc3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory");return r0;}
static inline int sc4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory");return r0;}

static int out;
static void puts_(const char *s){int n=0;while(s[n])n++;sc3(SYS_write,out,(int)s,n);}
static void putd(long v){
    static const long p10[10]={1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000};
    char b[24];int n=0,st=0,k;
    if(v<0){puts_("-");v=-v;}
    if(v==0){puts_("0");return;}
    for(k=9;k>=0;k--){int d=0;while(v>=p10[k]){v-=p10[k];d++;}if(d||st){b[n++]=(char)('0'+d);st=1;}}
    for(k=0;k<n;k++)sc3(SYS_write,out,(int)&b[k],1);
}
static int atoi_(const char *s){
    int v=0,sg=1;
    if(*s=='-'){sg=-1;s++;}
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return v*sg;
}
/* argv 来自 _start 的栈：用 main(argc,argv) 需要 crt，这里自带 _start */
long *g_sp;
static int my_argc(void){ return (int)g_sp[0]; }
static char *my_argv(int i){ return ((char **)(g_sp+1))[i]; }

void _start_c(void)
{
    out = sc3(SYS_open,(int)"/log/tids-test.log",0x241,0644);
    if (out < 0) { out = 1; }
    puts_("=== TIDS TEST ===\n");
    int argc = my_argc();
    puts_("argc="); putd(argc); puts_("\n");
    if (argc < 2) { puts_("no tids given\n"); }
    int i, ok = 0, fail = 0, det = 0;
    for (i = 1; i < argc; i++) {
        int tid = atoi_(my_argv(i));
        if (tid <= 0) continue;
        puts_("attach tid="); putd(tid); puts_(" ... ");
        if (sc4(SYS_ptrace, PTRACE_ATTACH, 0, tid, 0) == 0) {
            int st = 0;
            sc4(SYS_wait4, tid, (int)&st, __WALL, 0);
            puts_("OK\n"); ok++;
            if (sc4(SYS_ptrace, PTRACE_DETACH, 0, tid, 0) == 0) det++;
            else { puts_("DETACH FAILED\n"); }
        } else { puts_("FAIL\n"); fail++; }
    }
    puts_("attached="); putd(ok); puts_(" failed="); putd(fail);
    puts_(" detached="); putd(det); puts_("\n");
    puts_(ok > 0 ? "PTRACE_AVAILABLE\n" : "PTRACE_UNAVAILABLE\n");
    sc3(SYS_write, out, (int)"\n", 1);
}

__asm__(
".syntax unified\n"
".arm\n"
".text\n"
".global _start\n"
".type _start,%function\n"
"_start:\n"
"  mov r4, sp\n"
"  ldr r5, =g_sp\n"
"  str r4, [r5]\n"
"  bl _start_c\n"
"  mov r7, #1\n"
"  svc #0\n"
);

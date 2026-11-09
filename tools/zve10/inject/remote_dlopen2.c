/* remote_dlopen2 —— 纯 syscall 实现，无 libc 依赖
 * 硬编码 dlopen 偏移 = 0xbb0（相对 libdl 基址）
 */
#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_getpid     20
#define SYS_ptrace     26
#define SYS_pause      29
#define SYS_kill       37
#define SYS_brk        45
#define SYS_ioctl      54
#define SYS_wait4     114
#define SYS_pread64   180
#define SYS_pwrite64  181

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}

#define PTRACE_PEEKTEXT 1
#define PTRACE_PEEKDATA 2
#define PTRACE_POKETEXT 4
#define PTRACE_POKEDATA 5
#define PTRACE_GETREGS 12
#define PTRACE_SETREGS 13
#define PTRACE_ATTACH 16
#define PTRACE_DETACH 17
#define PTRACE_CONT 7

static char B[4096]; static int L;
static void s(const char*x){while(*x&&L<4000)B[L++]=*x++;}
static void iv(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void hx(unsigned v){s("0x");char t[10];int n=0;if(!v)t[n++]='0';while(v&&n<9){int d=v&15;t[n++]=d<10?'0'+d:'a'+d-10;v>>=4;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}
static int sl(const char*p){int n=0;while(p[n])n++;return n;}

struct regs { unsigned long u[18]; };

int main(int argc, char** argv)
{
    if (argc < 3) { s("usage: remote_dlopen2 <pid> <so>\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int pid = 0;
    { const char *p = argv[1]; while (*p >= '0' && *p <= '9') pid = pid*10 + (*p++ - '0'); }
    const char *sopath = argv[2];

    s("target pid="); iv(pid); nl();
    s("so="); s(sopath); nl();

    /* 读 /proc/PID/maps 找 libdl 基址和可写区 */
    char mp[64]; int k=0;
    { const char *p="/proc/"; while(*p)mp[k++]=*p++;
      char t[12];int tn=0;int v=pid;while(v){t[tn++]='0'+(v%10);v/=10;}while(tn)mp[k++]=t[--tn];
      const char *q="/maps"; while(*q)mp[k++]=*q++; mp[k]=0; }

    int fd = sy3(SYS_open,(int)mp,0,0);
    if (fd < 0) { s("no maps\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    static char mbuf[8192];
    int n = sy3(SYS_read,fd,(int)mbuf,sizeof mbuf-1);
    sy1(SYS_close,fd);
    if (n <= 0) { s("maps read fail\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    mbuf[n]=0;

    unsigned long dlbase = 0, wbase = 0, wend = 0;
    {
        int i = 0;
        while (i < n) {
            int ls = i; while (i < n && mbuf[i] != '\n') i++;
            int le = i; if (i < n) i++;
            if (le - ls < 20) continue;

            /* 地址1 */
            unsigned long a=0,b=0; int j=ls;
            while (j<le && mbuf[j]!='-') { char c=mbuf[j];
                if(c>='0'&&c<='9')a=a*16+(c-'0');
                else if(c>='a'&&c<='f')a=a*16+(c-'a'+10); j++; }
            if (j>=le) continue; j++;
            while (j<le) { char c=mbuf[j];
                if(c>='0'&&c<='9')b=b*16+(c-'0');
                else if(c>='a'&&c<='f')b=b*16+(c-'a'+10); else break; j++; }
            if (j+4 > le) continue;

            /* perm: r w x p  （第 4 个字符是 p 或 s） */
            char p0=mbuf[j], p1=mbuf[j+1], p2=mbuf[j+2];

            /* 找行尾的路径关键字 */
            int hasdl=0, hasstack=0;
            for (int t2=j; t2+8 < le; t2++) {
                if (mbuf[t2]=='l'&&mbuf[t2+1]=='i'&&mbuf[t2+2]=='b'&&
                    mbuf[t2+3]=='d'&&mbuf[t2+4]=='l'&&mbuf[t2+5]=='-') hasdl=1;
                if (mbuf[t2]=='['&&mbuf[t2+1]=='s'&&mbuf[t2+2]=='t'&&mbuf[t2+3]=='a'&&
                    mbuf[t2+4]=='c'&&mbuf[t2+5]=='k'&&mbuf[t2+6]==']') hasstack=1;
            }

            if (hasdl && p0=='r' && p1=='-' && p2=='x') dlbase = a;
            if (hasstack && p0=='r' && p1=='w') { wbase = a; wend = b; }
        }
    }
    s("libdl base="); hx(dlbase); nl();
    s("stack="); hx(wbase); s("-"); hx(wend); nl();
    if (!dlbase) { s("no libdl\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    if (!wend) { s("no stack\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    unsigned long dlopen_addr = dlbase + 0xbb0;
    s("dlopen="); hx(dlopen_addr); nl();

    /* 选位置：栈末尾 */
    unsigned long spot = (wend - 0x1000) & ~0xFUL;
    unsigned long path_addr = spot;
    unsigned long stub_addr = (spot + 0x200) & ~0xFUL;
    unsigned long save_addr = (spot + 0x400) & ~0xFUL;

    /* 打开 /proc/PID/mem */
    char memp[64]; k=0;
    { const char *p="/proc/"; while(*p)memp[k++]=*p++;
      char t[12];int tn=0;int v=pid;while(v){t[tn++]='0'+(v%10);v/=10;}while(tn)memp[k++]=t[--tn];
      const char *q="/mem"; while(*q)memp[k++]=*q++; memp[k]=0; }

    int mfd = sy3(SYS_open,(int)memp,2 /*O_RDWR*/,0);  /* O_RDWR=2 */
    if (mfd < 0) { s("no mem fd\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    /* ATTACH */
    int r = sy4(SYS_ptrace, PTRACE_ATTACH, pid, 0, 0);
    s("attach="); iv(r); nl();
    if (r < 0) { s("attach fail\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    int st = 0;
    sy4(SYS_wait4, pid, (int)&st, 0, 0);

    struct regs rg;
    r = sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&rg);
    s("getregs="); iv(r); s(" pc="); hx(rg.u[15]); nl();
    if (r < 0) { sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); return 1; }

    /* 保存 */
    sy4(SYS_pwrite64, mfd, (int)&rg, sizeof rg, save_addr);

    /* 写路径 */
    sy4(SYS_pwrite64, mfd, (int)sopath, sl(sopath)+1, path_addr);

    /* 构造 ARM stub */
    unsigned int stub[16]; int sn=0;
    stub[sn++] = 0xE92D500F;                      /* push {r0-r3,r12,lr} */
    unsigned lo = path_addr & 0xFFFF, hi = (path_addr>>16)&0xFFFF;
    stub[sn++] = 0xE3000000 | (((lo>>12)&0xF)<<16) | (lo&0xFFF);
    stub[sn++] = 0xE3400000 | (((hi>>12)&0xF)<<16) | (hi&0xFFF);
    stub[sn++] = 0xE3A01002;                      /* mov r1,#2 */
    unsigned dlo = dlopen_addr & 0xFFFF, dhi = (dlopen_addr>>16)&0xFFFF;
    stub[sn++] = 0xE30CC000 | (((dlo>>12)&0xF)<<16) | (dlo&0xFFF);
    stub[sn++] = 0xE34CC000 | (((dhi>>12)&0xF)<<16) | (dhi&0xFFF);
    stub[sn++] = 0xE12FFF3C;                      /* blx r12 */
    stub[sn++] = 0xE8BD500F;                      /* pop {r0-r3,r12,lr} */
    stub[sn++] = 0xE1200070;                      /* bkpt #0 */

    r = sy4(SYS_pwrite64, mfd, (int)stub, sn*4, stub_addr);
    s("stub write="); iv(r); nl();

    /* 设 PC/SP */
    rg.u[15] = stub_addr | 0;   /* ARM 模式，不要 Thumb 位 */
    rg.u[13] = (save_addr - 0x100) & ~0xFUL;
    /* 清 Thumb 位（CPSR T bit）*/
    rg.u[16] &= ~0x20UL;

    r = sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&rg);
    s("setregs="); iv(r); nl();
    if (r < 0) { sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); return 1; }

    s("continuing...\n");
    sy4(SYS_ptrace, PTRACE_CONT, pid, 0, 0);
    st = 0;
    sy4(SYS_wait4, pid, (int)&st, 0, 0);
    s("stopped status="); iv(st); nl();

    struct regs after;
    r = sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&after);
    if (r == 0) { s("r0(dlopen)="); hx(after.u[0]); nl(); }

    /* 恢复 */
    struct regs saved;
    sy4(SYS_pread64, mfd, (int)&saved, sizeof saved, save_addr);
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&saved);
    sy4(SYS_ptrace, PTRACE_DETACH, pid, 0, 0);
    s("restored & detached\n");

    sy1(SYS_close, mfd);
    sy3(SYS_write, 2, (int)B, L);
    return 0;
}

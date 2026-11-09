/* rd5 —— 直接设 PC 调 dlopen，LR=0 靠 SIGSEGV 捕获返回
 * 不需要任何可执行内存：只往栈（rw）写路径字符串，执行的是 libc 自己的代码
 * 用法: rd5 <pid> <libcbase> <stacktop> <so路径>
 */
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_ptrace     26
#define SYS_wait4     114
#define SYS_pread64   180
#define SYS_pwrite64  181

static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

#define PTRACE_GETREGS 12
#define PTRACE_SETREGS 13
#define PTRACE_ATTACH  16
#define PTRACE_DETACH  17
#define PTRACE_CONT     7

static char B[2048]; static int L;
static void s(const char*x){while(*x&&L<2000)B[L++]=*x++;}
static void iv(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void hx(unsigned v){s("0x");char t[10];int n=0;if(!v)t[n++]='0';while(v&&n<9){int d=v&15;t[n++]=d<10?'0'+d:'a'+d-10;v>>=4;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}
static int sl(const char*p){int n=0;while(p[n])n++;return n;}
static unsigned long ph(const char*z){unsigned long v=0;while(*z){char c=*z++;if(c>='0'&&c<='9')v=v*16+(c-'0');else if(c>='a'&&c<='f')v=v*16+(c-'a'+10);else if(c>='A'&&c<='F')v=v*16+(c-'A'+10);else break;}return v;}
static int pi(const char*z){int v=0;while(*z>='0'&&*z<='9')v=v*10+(*z++-'0');return v;}

struct regs { unsigned long u[18]; };

int main(int argc, char** argv)
{
    if (argc < 5) { s("usage: rd5 <pid> <libcbase> <stacktop> <so>\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int pid = pi(argv[1]);
    unsigned long libcbase = ph(argv[2]);
    unsigned long stacktop = ph(argv[3]);
    const char *sopath = argv[4];

    s("pid="); iv(pid); s(" libc="); hx(libcbase); s(" stk="); hx(stacktop); nl();

    char memp[64]; int k=0;
    { const char *p="/proc/"; while(*p)memp[k++]=*p++;
      char t[12];int tn=0;int v=pid;while(v){t[tn++]='0'+(v%10);v/=10;}while(tn)memp[k++]=t[--tn];
      const char *q="/mem"; while(*q)memp[k++]=*q++; memp[k]=0; }
    int mfd = sy3(SYS_open,(int)memp,2,0);
    if (mfd < 0) { s("no mem\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    unsigned long path_addr = (stacktop - 0x800) & ~0xFUL;
    s("path@"); hx(path_addr); nl();

    int r = sy4(SYS_ptrace, PTRACE_ATTACH, pid, 0, 0);
    if (r < 0) { s("attach fail\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);
    s("attached\n");

    /* ATTACH 之后写路径（用 POKETEXT 兜底） */
    { int wr = sy5(SYS_pwrite64, mfd, (int)sopath, sl(sopath)+1, path_addr, 0);
      s("write(mem)="); iv(wr); nl();
      if (wr <= 0) {
          /* 用 ptrace POKETEXT 一个字节一个字节写 */
          for (int i = 0; i < sl(sopath)+1; i += 4) {
              unsigned long wa = (path_addr + i) & ~3UL;
              long val = sy4(SYS_ptrace, 1 /*PEEKTEXT*/, pid, (int)wa, 0);
              unsigned char *pv = (unsigned char*)&val;
              for (int j = 0; j < 4 && (i+j) < sl(sopath)+1; j++)
                  pv[(path_addr+i+j) - wa] = (unsigned char)sopath[i+j];
              sy4(SYS_ptrace, 4 /*POKETEXT*/, pid, (int)wa, (int)val);
          }
          s("wrote via POKETEXT\n");
      }
    }

    struct regs rg, save;
    if (sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&rg) < 0) { s("getregs fail\n"); sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); return 1; }
    for (int i=0;i<18;i++) save.u[i]=rg.u[i];

    /* 直接调 __libc_dlopen_mode(path, 2)
     * libc-2.11.2.so: __libc_dlopen_mode @ 0xc85fc (Thumb → +1)
     * 也可以试 dlopen @ libdl 0xbb0+1 */
    /* libdl 的 dlopen：libdl-2.11.2.so 里 0xbb0（Thumb → 0xbb1）
     * libcbase 参数现在是【libdl 基址】 */
    unsigned long fn = libcbase + 0xbb0 + 1;
    s("dlopen(libdl)="); hx(fn); nl();

    rg.u[0]  = path_addr;
    rg.u[1]  = 2;                 /* RTLD_NOW */
    rg.u[15] = fn;                /* PC */
    rg.u[14] = 0;                 /* LR = 0 → 返回时 SIGSEGV */
    rg.u[16] &= ~0x20UL;          /* ARM 模式 */
    /* SP 保持在栈上不变 */
    rg.u[13] = (stacktop - 0xC00) & ~0xFUL;

    if (sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&rg) < 0) {
        s("setregs fail\n"); sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); sy3(SYS_write,2,(int)B,L); return 1;
    }
    s("cont...\n");
    sy4(SYS_ptrace, PTRACE_CONT, pid, 0, 0);
    st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);
    s("stopped, status="); iv(st); nl();

    struct regs after;
    if (sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&after) == 0) {
        s("r0="); hx(after.u[0]);
        s(" pc="); hx(after.u[15]);
        s(" cpsr="); hx(after.u[16]);
        nl();
        s("  r1="); hx(after.u[1]); s(" r2="); hx(after.u[2]); s(" r3="); hx(after.u[3]);
        s(" r4="); hx(after.u[4]); s(" sp="); hx(after.u[13]); nl();
        if (after.u[0] == path_addr) s("*** r0 还是 path 地址：函数可能没改 r0 ***\n");
        else if (after.u[0] > 0x1000 && after.u[0] < 0x80000000UL) s("*** 有效 handle ***\n");
        else s("*** 无效 ***\n");
    }

    /* 恢复 */
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&save);
    sy4(SYS_ptrace, PTRACE_DETACH, pid, 0, 0);
    s("restored\n");
    sy1(SYS_close, mfd);
    sy3(SYS_write, 2, (int)B, L);
    return 0;
}

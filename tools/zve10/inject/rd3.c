/* rd3 —— 地址从命令行传入，不在程序内解析 maps
 * 用法: rd3 <pid> <dlbase_hex> <stacktop_hex> <so路径>
 */
#define SYS_exit        1
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
#define PTRACE_ATTACH 16
#define PTRACE_DETACH 17
#define PTRACE_CONT 7

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
    if (argc < 5) { s("usage: rd3 <pid> <dlbase> <stacktop> <so>\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int pid = pi(argv[1]);
    unsigned long dlbase = ph(argv[2]);
    unsigned long stacktop = ph(argv[3]);
    const char *sopath = argv[4];

    s("pid="); iv(pid); s(" dlbase="); hx(dlbase); s(" stacktop="); hx(stacktop); nl();
    if (!dlbase || !stacktop) { sy3(SYS_write,2,(int)B,L); return 1; }

    /* 用 __libc_dlopen_mode（libc 里，不依赖 libdl 的 _dl_open 跳转）
     * libc-2.11.2.so: __libc_dlopen_mode @ 0xc85fc (Thumb → +1) */
    unsigned long dlopen_addr = dlbase + 0xc85fc + 1;
    s("__libc_dlopen_mode="); hx(dlopen_addr); nl();

    unsigned long spot = (stacktop - 0x2000) & ~0xFUL;
    unsigned long path_addr = spot;
    unsigned long stub_addr = (spot + 0x200) & ~0xFUL;
    unsigned long save_addr = (spot + 0x400) & ~0xFUL;

    char memp[64]; int k=0;
    { const char *p="/proc/"; while(*p)memp[k++]=*p++;
      char t[12];int tn=0;int v=pid;while(v){t[tn++]='0'+(v%10);v/=10;}while(tn)memp[k++]=t[--tn];
      const char *q="/mem"; while(*q)memp[k++]=*q++; memp[k]=0; }

    int mfd = sy3(SYS_open,(int)memp,2,0);
    if (mfd < 0) { s("no mem\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    int r = sy4(SYS_ptrace, PTRACE_ATTACH, pid, 0, 0);
    if (r < 0) { s("attach fail\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);

    struct regs rg;
    r = sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&rg);
    if (r < 0) { s("getregs fail\n"); sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); sy3(SYS_write,2,(int)B,L); return 1; }
    s("orig pc="); hx(rg.u[15]); nl();

    sy5(SYS_pwrite64, mfd, (int)&rg, sizeof rg, save_addr, 0);
    sy5(SYS_pwrite64, mfd, (int)sopath, sl(sopath)+1, path_addr, 0);

    unsigned int stub[16]; int sn=0;
    /* movw rd, #imm16 : cond=1110 0011 0000 imm4 | rd imm3 imm8 */
    /* 编码由 arm-linux-as 实测：
       movw r0,#0     = E3000000
       movt r0,#5EC9  = E3450EC9
       movw ip,#EBB1  = E30ECBB1
       movt ip,#0400  = E340C400
       ⇒ imm4 在 bit19:16，i 在 bit10，imm3 在 bit14:12，imm8 在 bit7:0 */
    #define ENC_MOVW(rd, v) (0xE3000000u | ((((v)>>12)&0xF)<<16) | ((rd)<<12) | ((v)&0xFFF))
    #define ENC_MOVT(rd, v) (0xE3400000u | ((((v)>>12)&0xF)<<16) | ((rd)<<12) | ((v)&0xFFF))

    stub[sn++] = 0xE92D500F;                       /* push {r0-r3,r12,lr} */
    stub[sn++] = ENC_MOVW(0, path_addr & 0xFFFF);
    stub[sn++] = ENC_MOVT(0, (path_addr>>16) & 0xFFFF);
    stub[sn++] = 0xE3A01002;                       /* mov r1, #2 (RTLD_NOW) */
    stub[sn++] = ENC_MOVW(12, dlopen_addr & 0xFFFF);
    stub[sn++] = ENC_MOVT(12, (dlopen_addr>>16) & 0xFFFF);
    stub[sn++] = 0xE12FFF3C;                       /* blx r12 */
    stub[sn++] = 0xE8BD500E;                       /* pop {r1-r3,r12,lr} —— 不恢复 r0！ */
    stub[sn++] = 0xE1200070;                       /* bkpt #0 */

    { s("stub hex:"); for(int q=0;q<sn;q++){s(" ");hx(stub[q]);} nl(); }
    sy5(SYS_pwrite64, mfd, (int)stub, sn*4, stub_addr, 0);
    s("stub@"); hx(stub_addr); s(" wrote "); iv(sn*4); s(" B"); nl();

    rg.u[15] = stub_addr;
    rg.u[13] = (save_addr - 0x100) & ~0xFUL;
    rg.u[16] &= ~0x20UL;
    r = sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&rg);
    if (r < 0) { s("setregs fail\n"); sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); sy3(SYS_write,2,(int)B,L); return 1; }

    s("cont\n");
    sy4(SYS_ptrace, PTRACE_CONT, pid, 0, 0);
    st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);
    s("stop st="); iv(st); nl();

    struct regs after;
    if (sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&after) == 0) {
        s("RESULT r0="); hx(after.u[0]);
        s(" r1="); hx(after.u[1]);
        s(" r12="); hx(after.u[12]);
        s(" pc="); hx(after.u[15]);
        s(" cpsr="); hx(after.u[16]);
        nl();
    }

    struct regs saved;
    sy5(SYS_pread64, mfd, (int)&saved, sizeof saved, save_addr, 0);
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&saved);
    sy4(SYS_ptrace, PTRACE_DETACH, pid, 0, 0);
    s("done\n");
    sy1(SYS_close, mfd);
    sy3(SYS_write, 2, (int)B, L);
    return 0;
}

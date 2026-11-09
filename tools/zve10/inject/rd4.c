/* rd4 —— 用单条 svc 指令执行 syscall（不需要可执行内存）
 * 步骤：
 *   1. 找 im.elf 的可执行区（0x8000-0xd000），找一段可安全覆盖的指令
 *   2. 写 svc #0
 *   3. 设置 r7/r0/r1/r2 调 mprotect 让栈变 rwx
 *   4. 单步执行（PTRACE_SINGLESTEP）
 *   5. 恢复原指令
 *   6. 现在栈可执行了，写 ARM stub 调 __libc_dlopen_mode
 *   7. 执行、读 r0、恢复
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
#define PTRACE_SINGLESTEP 9

static char B[2048]; static int L;
static void s(const char*x){while(*x&&L<2000)B[L++]=*x++;}
static void iv(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void hx(unsigned v){s("0x");char t[10];int n=0;if(!v)t[n++]='0';while(v&&n<9){int d=v&15;t[n++]=d<10?'0'+d:'a'+d-10;v>>=4;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}
static int sl(const char*p){int n=0;while(p[n])n++;return n;}
static unsigned long ph(const char*z){unsigned long v=0;while(*z){char c=*z++;if(c>='0'&&c<='9')v=v*16+(c-'0');else if(c>='a'&&c<='f')v=v*16+(c-'a'+10);else if(c>='A'&&c<='F')v=v*16+(c-'A'+10);else break;}return v;}
static int pi(const char*z){int v=0;while(*z>='0'&&*z<='9')v=v*10+(*z++-'0');return v;}

struct regs { unsigned long u[18]; };

static void *memcpy(void *d, const void *sk, unsigned n) {
    unsigned char *dd = d; const unsigned char *ss = sk;
    while (n--) *dd++ = *ss++;
    return d;
}

#define ENC_MOVW(rd, v) (0xE3000000u | ((((v)>>12)&0xF)<<16) | ((rd)<<12) | ((v)&0xFFF))
#define ENC_MOVT(rd, v) (0xE3400000u | ((((v)>>12)&0xF)<<16) | ((rd)<<12) | ((v)&0xFFF))

int main(int argc, char** argv)
{
    if (argc < 6) { s("usage: rd4 <pid> <libcbase> <stacktop> <scratch_exec> <so>\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int pid = pi(argv[1]);
    unsigned long libcbase = ph(argv[2]);
    unsigned long stacktop = ph(argv[3]);
    unsigned long scratch = ph(argv[4]);   /* 目标里可执行的、可安全覆盖的位置 */
    const char *sopath = argv[5];

    s("pid="); iv(pid); s(" libc="); hx(libcbase); s(" stk="); hx(stacktop); s(" scratch="); hx(scratch); nl();

    char memp[64]; int k=0;
    { const char *p="/proc/"; while(*p)memp[k++]=*p++;
      char t[12];int tn=0;int v=pid;while(v){t[tn++]='0'+(v%10);v/=10;}while(tn)memp[k++]=t[--tn];
      const char *q="/mem"; while(*q)memp[k++]=*q++; memp[k]=0; }
    int mfd = sy3(SYS_open,(int)memp,2,0);
    if (mfd < 0) { s("no mem\n"); sy3(SYS_write,2,(int)B,L); return 1; }

    int r = sy4(SYS_ptrace, PTRACE_ATTACH, pid, 0, 0);
    if (r < 0) { s("attach fail\n"); sy3(SYS_write,2,(int)B,L); return 1; }
    int st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);

    struct regs rg, save;
    if (sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&rg) < 0) { s("getregs fail\n"); sy4(SYS_ptrace,PTRACE_DETACH,pid,0,0); return 1; }
    for (int _i=0;_i<18;_i++) save.u[_i] = rg.u[_i];

    /* ---- 步骤 1：用 scratch 处的一条 svc 指令调 mprotect 让栈 rwx ---- */
    unsigned long page = stacktop - 0x20000;
    s("mprotect("); hx(page); s(", 0x20000, 7)"); nl();

    unsigned int svc = 0xEF000000;      /* svc #0 */
    unsigned int orig;
    sy5(SYS_pread64, mfd, (int)&orig, 4, scratch, 0);
    s("scratch orig="); hx(orig); nl();
    sy5(SYS_pwrite64, mfd, (int)&svc, 4, scratch, 0);

    /* 设寄存器调 mprotect(SYS=125) */
    rg.u[7]  = 125;             /* SYS_mprotect */
    rg.u[0]  = page;
    rg.u[1]  = 0x20000;
    rg.u[2]  = 7;               /* PROT_READ|WRITE|EXEC */
    rg.u[15] = scratch;
    rg.u[16] &= ~0x20UL;        /* ARM 模式 */
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&rg);
    sy4(SYS_ptrace, PTRACE_SINGLESTEP, pid, 0, 0);
    st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);

    struct regs after1;
    sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&after1);
    s("mprotect ret="); hx(after1.u[0]); nl();

    /* 恢复 scratch */
    sy5(SYS_pwrite64, mfd, (int)&orig, 4, scratch, 0);

    /* ---- 步骤 2：现在栈可执行了，写 stub ---- */
    unsigned long stub_addr = (stacktop - 0x1800) & ~0xFUL;
    unsigned long path_addr = (stacktop - 0x1000) & ~0xFUL;
    unsigned long dl_addr = libcbase + 0xc85fc + 1;

    sy5(SYS_pwrite64, mfd, (int)sopath, sl(sopath)+1, path_addr, 0);

    unsigned int stub[16]; int sn=0;
    stub[sn++] = 0xE92D500F;
    stub[sn++] = ENC_MOVW(0, path_addr & 0xFFFF);
    stub[sn++] = ENC_MOVT(0, (path_addr>>16)&0xFFFF);
    stub[sn++] = 0xE3A01002;
    stub[sn++] = ENC_MOVW(12, dl_addr & 0xFFFF);
    stub[sn++] = ENC_MOVT(12, (dl_addr>>16)&0xFFFF);
    stub[sn++] = 0xE12FFF3C;
    stub[sn++] = 0xE8BD500E;
    stub[sn++] = 0xE1200070;
    sy5(SYS_pwrite64, mfd, (int)stub, sn*4, stub_addr, 0);
    s("stub@"); hx(stub_addr); nl();

    /* 执行 stub */
    struct regs rg2; for (int _i=0;_i<18;_i++) rg2.u[_i] = save.u[_i];
    rg2.u[15] = stub_addr;
    rg2.u[13] = (path_addr - 0x200) & ~0xFUL;
    rg2.u[16] &= ~0x20UL;
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&rg2);
    sy4(SYS_ptrace, PTRACE_CONT, pid, 0, 0);
    st=0; sy4(SYS_wait4, pid, (int)&st, 0, 0);
    s("stop="); iv(st); nl();

    struct regs after2;
    sy4(SYS_ptrace, PTRACE_GETREGS, pid, 0, (int)&after2);
    s("RESULT r0="); hx(after2.u[0]); s(" pc="); hx(after2.u[15]); s(" cpsr="); hx(after2.u[16]); nl();

    /* 恢复全部 */
    sy4(SYS_ptrace, PTRACE_SETREGS, pid, 0, (int)&save);
    sy4(SYS_ptrace, PTRACE_DETACH, pid, 0, 0);
    s("done\n");
    sy1(SYS_close, mfd);
    sy3(SYS_write, 2, (int)B, L);
    return 0;
}

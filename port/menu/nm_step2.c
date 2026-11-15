/* 步骤 2：一次性安全探针（带自动拆除）。
 *
 * 设计目标：证明「bootin 的 preload 白名单机制」能把我们的库送进 im.elf，
 * 同时在**证明成功的同一瞬间自动拆除**，确保不会造成重启循环。
 *
 * 做法：
 *   1. 只服务 root 进程
 *   2. 检测自己的 comm：
 *        = "im.elf"  ⇒ 命中目标：写日志 + **立刻把 dmode 改回 "3"**（自动拆除）
 *        其它        ⇒ 写日志（证明库能加载），然后返回
 *   3. 全程只 open/write/close，不 fork、不 dlopen、不建线程
 *
 * 为什么"立刻拆除"能保证安全：
 *   - 若 im.elf 之后崩溃，下次重启 bootin 读到的 dmode 已是 "3"
 *     ⇒ 不会再 preload ⇒ im.elf 正常启动 ⇒ 相机自愈
 *   - 若 im.elf 正常，我们也已经拿到"库确实进了 im.elf"的证据
 */
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_getuid     24
#define SYS_getpid     20
#define SYS_gettid    224

#define LOG   "/log/nm-step2.log"
#define KILL  "/log/nm-off"
#define DMODE "/setting/mode/dmode"

static inline int sc0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory");return r0;}
static inline int sc1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory");return r0;}
static inline int sc2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory");return r0;}
static inline int sc3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory");return r0;}

static void putd(long v, char **pp){
    static const long p10[10]={1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000};
    char b[16]; int n=0,st=0,k;
    if(v<0){ **pp='-'; (*pp)++; v=-v; }
    if(v==0){ **pp='0'; (*pp)++; return; }
    for(k=9;k>=0;k--){ int d=0; while(v>=p10[k]){ v-=p10[k]; d++; } if(d||st){ b[n++]=(char)('0'+d); st=1; } }
    for(k=0;k<n;k++){ **pp=b[k]; (*pp)++; }
}

__attribute__((constructor)) static void nm_step2(void)
{
    if (sc0(SYS_getuid) != 0) return;
    if (sc1(SYS_open, (int)KILL) >= 0) return;      /* killswitch */

    int pid = sc0(SYS_getpid);
    int tid = sc0(SYS_gettid);

    char comm[48];
    int clen = 0;
    int cfd = sc2(SYS_open, (int)"/proc/self/comm", 0);
    if (cfd >= 0) {
        clen = sc3(SYS_read, cfd, (int)comm, 47);
        sc1(SYS_close, cfd);
        if (clen < 0) clen = 0;
        int i; for (i = 0; i < clen; i++) if (comm[i]=='\n') { comm[i]=0; break; }
        if (i >= clen) comm[clen] = 0;
    } else comm[0]=0;

    int is_imelf = 1;
    { const char *t = "im.elf"; int i;
      for (i = 0; i < 6; i++) if (comm[i] != t[i]) { is_imelf = 0; break; } }

    /* 写日志 */
    int fd = sc3(SYS_open, (int)LOG, 0x441, 0644);
    if (fd >= 0) {
        char buf[200]; char *p = buf;
        *p++='N';*p++='M';*p++='2';*p++=' ';
        if (is_imelf) { *p++='H';*p++='I';*p++='T'; } else { *p++='-';*p++='-';*p++='-'; }
        *p++=' ';*p++='p';*p++='i';*p++='d';*p++='=';
        putd(pid,&p);
        *p++=' ';*p++='t';*p++='i';*p++='d';*p++='=';
        putd(tid,&p);
        *p++=' ';*p++='c';*p++='o';*p++='m';*p++='m';*p++='=';
        { int i; for (i=0;i<clen && comm[i];i++) *p++=comm[i]; }
        *p++='\n';
        sc3(SYS_write, fd, (int)buf, (int)(p-buf));
        sc1(SYS_close, fd);
    }

    /* ★ 命中 im.elf：立刻拆除 dmode，保证即使后续崩溃也能自愈 */
    if (is_imelf) {
        /* 先把原内容备份到 /log/nm-dmode.bak（只做一次，不覆盖） */
        {
            int src = sc2(SYS_open, (int)DMODE, 0);
            if (src >= 0) {
                char ob[64]; int ol = sc3(SYS_read, src, (int)ob, 63);
                sc1(SYS_close, src);
                if (ol > 0) {
                    int bk = sc3(SYS_open, (int)"/log/nm-dmode.bak", 0x441, 0644);
                    if (bk >= 0) { sc3(SYS_write, bk, (int)ob, ol); sc1(SYS_close, bk); }
                }
            }
        }
        int d = sc3(SYS_open, (int)DMODE, 0x241, 0644);  /* O_WRONLY|O_CREAT|O_TRUNC */
        if (d >= 0) {
            char one = '3';
            sc3(SYS_write, d, (int)&one, 1);
            sc1(SYS_close, d);
            /* 记录拆除成功 */
            int f2 = sc3(SYS_open, (int)LOG, 0x441, 0644);
            if (f2 >= 0) {
                sc3(SYS_write, f2, (int)"NM2 DMODE_REVERTED_TO_3\n", 26);
                sc1(SYS_close, f2);
            }
        }
    }
}

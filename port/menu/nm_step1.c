/* 步骤 1：最小、绝对安全的预加载库。
 *
 * 目标：证明「改 /etc/ld.so.preload + 重拉 im.elf」这条链是安全的。
 *
 * 这个库**不调用任何索尼 API**，只做：
 *   1. 不是 root 就直接返回（让非 root 进程完全不受影响）
 *   2. 检查 killswitch 文件 /log/nm-off（存在就什么都不做）
 *   3. 往 /log/nm-step1.log 写一行（记录 pid / 进程名 / tid）
 *   4. 立刻返回，不 fork、不 dlopen、不开线程
 *
 * 全部用裸 syscall，不依赖相机 glibc 的任何符号。
 * 即使某处失败，也只是少写一行日志。
 */
#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_getuid     24
#define SYS_getpid     20
#define SYS_gettid    224

static inline int sc0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory");return r0;}
static inline int sc1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory");return r0;}
static inline int sc2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory");return r0;}
static inline int sc3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory");return r0;}

#define LOG "/log/nm-step1.log"
#define OFF "/log/nm-off"

/* 极简 putd：只打 0..9999 */
static void putd(long v, char **pp){
    static const long p10[5]={1,10,100,1000,10000};
    char b[16]; int n=0,st=0,k;
    if(v<0){ **pp='-'; (*pp)++; v=-v; }
    if(v==0){ **pp='0'; (*pp)++; return; }
    for(k=4;k>=0;k--){
        int d=0;
        while(v>=p10[k]){ v-=p10[k]; d++; }
        if(d||st){ b[n++]=(char)('0'+d); st=1; }
    }
    for(k=0;k<n;k++){ **pp=b[k]; (*pp)++; }
}

__attribute__((constructor)) static void nm_step1(void)
{
    /* 1. 只服务 root 进程 */
    if (sc0(SYS_getuid) != 0) return;

    /* 2. killswitch：/log/nm-off 存在就不做任何事 */
    if (sc1(SYS_open, (int)OFF) >= 0) return;

    int pid  = sc0(SYS_getpid);
    int tid  = sc0(SYS_gettid);

    /* 读自己的 comm（进程名），失败就算了 */
    char comm[32];
    int cfd = sc2(SYS_open, (int)"/proc/self/comm", 0);
    int clen = 0;
    if (cfd >= 0) {
        clen = sc3(SYS_read, cfd, (int)comm, 31);
        sc1(SYS_close, cfd);
        if (clen < 0) clen = 0;
        int i;
        for (i = 0; i < clen; i++) if (comm[i] == '\n') { comm[i] = 0; break; }
        if (i >= clen) comm[clen] = 0;
    } else {
        comm[0] = 0;
    }

    /* 3. 写日志：一行 */
    int fd = sc3(SYS_open, (int)LOG, 0x241 /* O_WRONLY|O_CREAT|O_APPEND */, 0644);
    if (fd < 0) return;   /* 打不开就彻底安静地退出 */

    char buf[160]; char *p = buf;
    *p++='N';*p++='M';*p++='1';*p++=' ';*p++='p';*p++='i';*p++='d';*p++='=';
    putd(pid, &p);
    *p++=' ';*p++='t';*p++='i';*p++='d';*p++='=';
    putd(tid, &p);
    *p++=' ';*p++='c';*p++='o';*p++='m';*p++='m';*p++='=';
    { int i; for (i = 0; i < clen && comm[i]; i++) *p++ = comm[i]; }
    *p++='\n';

    sc3(SYS_write, fd, (int)buf, (int)(p - buf));
    sc1(SYS_close, fd);
    /* 4. 立刻返回 —— 不 fork / 不 dlopen / 不建线程 */
}

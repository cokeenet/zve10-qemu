/* 诊断 agent 为什么没起来：查日志、查 socket 锁、查进程 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_mount 21
#define SYS_umount2 52
#define SYS_mkdir 39
#define SYS_fsync 118
#define SYS_getdents64 217
#define SYS_lseek 19

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static char B[16384]; static int L;
static void s(const char*x){while(*x&&L<16200)B[L++]=*x++;}
static void i(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}
static int sl(const char*p){int n=0;while(p[n])n++;return n;}

static int wf(const char*p,const char*d,int n){
    int fd=sy3(SYS_open,(int)p,0x241,0644);
    if(fd<0)return fd;
    int o=0;
    while(o<n){int r=sy3(SYS_write,fd,(int)(d+o),n-o);if(r<=0)break;o+=r;}
    sy1(SYS_fsync,fd);sy1(SYS_close,fd);
    return o;
}
static int ex(const char*p){int fd=sy3(SYS_open,(int)p,0,0);if(fd<0)return 0;sy1(SYS_close,fd);return 1;}

/* 打印文件内容 */
static void dump(const char *path, int maxb)
{
    s("--- "); s(path); s(" ---\n");
    int fd=sy3(SYS_open,(int)path,0,0);
    if(fd<0){ s("  (打不开)\n"); return; }
    char b[2048]; int tot=0;
    while(tot<maxb){
        int n=sy3(SYS_read,fd,(int)b,sizeof b);
        if(n<=0)break;
        for(int k=0;k<n&&L<16100;k++){char c=b[k];B[L++]=c;}
        tot+=n;
    }
    sy1(SYS_close,fd);
    if(tot==0) s("  (空文件)\n");
    if(L>0&&B[L-1]!='\n')nl();
}

static void work(void)
{
    s("=== DIAG AGENT ===\n");

    /* ① preload 配置 */
    dump("/setting/mode/preload", 200);
    dump("/setting/mode/dmode", 50);
    s("\n");

    /* ② agent 文件 */
    s("agent exists="); i(ex("/log/agent_v14.so")); nl();
    s("agent size=");
    { int fd=sy3(SYS_open,(int)"/log/agent_v14.so",0,0);
      if(fd>=0){ int n=sy3(SYS_lseek,fd,0,2); sy1(SYS_close,fd); i(n); } else s("?");
      nl(); }
    s("\n");

    /* ③ agent 日志（最关键！）*/
    dump("/log/zve10_agent.log", 6000);
    s("\n");

    /* ④ watchdog 日志 */
    dump("/log/zve10_watchdog.txt", 1500);
    dump("/log/zve10_net.log", 1500);
    s("\n");

    /* ⑤ 是否有 agent 进程在跑 */
    s("=== /proc 里的进程 ===\n");
    {
        int dfd=sy3(SYS_open,(int)"/proc",0x4000,0);   /* O_DIRECTORY */
        if(dfd>=0){
            static char db[4096]; int n;
            int cnt=0;
            while((n=sy3(SYS_getdents64,dfd,(int)db,sizeof db))>0 && cnt<40){
                for(int off=0;off<n;){
                    unsigned short rl=*(unsigned short*)(db+off+16);
                    char *nm=db+off+19;
                    if(rl==0)break;
                    if(nm[0]>='1'&&nm[0]<='9'){
                        char p[64]; int pl=0;
                        const char *pf="/proc/"; while(*pf)p[pl++]=*pf++;
                        for(int j=0;nm[j]&&pl<40;j++)p[pl++]=nm[j];
                        const char *cf="/cmdline"; for(int j=0;cf[j];j++)p[pl++]=cf[j];
                        p[pl]=0;
                        int fd=sy3(SYS_open,(int)p,0,0);
                        if(fd>=0){
                            char cb[256]; int cn=sy3(SYS_read,fd,(int)cb,250); sy1(SYS_close,fd);
                            if(cn>0){
                                s("  "); 
                                for(int j=0;j<cn;j++){char c=cb[j];B[L++]=(c>=32&&c<127)?c:' ';}
                                s("\n"); cnt++;
                            }
                        }
                    }
                    off+=rl;
                }
            }
            sy1(SYS_close,dfd);
        } else s("  /proc 打不开\n");
    }
    nl();

    /* ⑥ 网络接口 */
    dump("/proc/net/dev", 1200);
    s("\n");

    /* ⑦ TCP 监听 */
    dump("/proc/net/tcp", 1500);

    s("\n=== DIAG END ===\n");

    /* 写出去：SD + /log + /setting */
    sy2(SYS_mkdir,(int)"/tmp/dd",0755);
    if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/dd",(int)"vfat",0,0)==0){
        wf("/tmp/dd/AGENT_DIAG.txt",B,L);
        sy1(SYS_umount2,(int)"/tmp/dd");
    }
    wf("/log/agent_diag.txt",B,L);
    wf("/setting/agent_diag.txt",B,L);
}

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){ work(); sy1(SYS_exit,0); for(;;){} }
}

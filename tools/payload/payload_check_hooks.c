/* 检查所有开机自动执行的东西 */
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
static int ex(const char*p){int fd=sy3(SYS_open,(int)p,0,0);if(fd<0)return 0;sy1(SYS_close,fd);return 1;}
static int wf(const char*p,const char*d,int n){
    int fd=sy3(SYS_open,(int)p,0x241,0644);
    if(fd<0)return fd;
    int o=0;
    while(o<n){int r=sy3(SYS_write,fd,(int)(d+o),n-o);if(r<=0)break;o+=r;}
    sy1(SYS_fsync,fd);sy1(SYS_close,fd);
    return o;
}
static void dump(const char*path,int maxb){
    s("--- ");s(path);s(" ---\n");
    int fd=sy3(SYS_open,(int)path,0,0);
    if(fd<0){s("  (无)\n");return;}
    char b[1024];int tot=0;
    while(tot<maxb){int n=sy3(SYS_read,fd,(int)b,sizeof b);if(n<=0)break;
        for(int k=0;k<n&&L<16100;k++)B[L++]=b[k];tot+=n;}
    sy1(SYS_close,fd);
    if(tot==0)s("  (空)\n");
    if(L>0&&B[L-1]!='\n')nl();
}
static void lsdir(const char*path){
    s("--- ls ");s(path);s(" ---\n");
    int fd=sy3(SYS_open,(int)path,0x4000,0);
    if(fd<0){s("  (无)\n");return;}
    static char db[4096];int n;
    while((n=sy3(SYS_getdents64,fd,(int)db,sizeof db))>0){
        for(int off=0;off<n;){
            unsigned short rl=*(unsigned short*)(db+off+16);
            char*nm=db+off+19;
            if(rl==0)break;
            if(nm[0]!='.'){s("  ");s(nm);nl();}
            off+=rl;
        }
    }
    sy1(SYS_close,fd);
}

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){
        s("=== HOOKS CHECK ===\n\n");

        dump("/etc/ld.so.preload",300);
        nl();
        dump("/setting/mode/preload",300);
        dump("/setting/mode/dmode",64);
        nl();

        s("### /log 下的 .so ###\n");
        lsdir("/log");
        nl();

        s("### /setting/mode ###\n");
        lsdir("/setting/mode");
        nl();

        dump("/setting/mode/zve10_net",64);
        nl();

        s("### /system 或启动脚本 ###\n");
        dump("/etc/init.d/rcS",2000);
        nl();

        s("=== END ===\n");

        sy2(SYS_mkdir,(int)"/tmp/hc",0755);
        if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/hc",(int)"vfat",0,0)==0){
            wf("/tmp/hc/HOOKS.txt",B,L);
            sy1(SYS_umount2,(int)"/tmp/hc");
        }
        wf("/log/hooks.txt",B,L);
        sy1(SYS_exit,0);
        for(;;){}
    }
}

/* 探测：payload 是否执行 + SD 能否在 USB 模式下挂载 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_mknod 14
#define SYS_mount 21
#define SYS_umount2 52
#define SYS_mkdir 39
#define SYS_fsync 118

static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}
static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}

static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static char B[4096]; static int L;
static void s(const char*x){while(*x&&L<4000)B[L++]=*x++;}
static void i(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}

static int wf(const char*p,const char*d,int n){
    int fd=sy3(SYS_open,(int)p,0x241,0644);
    if(fd<0)return fd;
    int o=0;
    while(o<n){int r=sy3(SYS_write,fd,(int)(d+o),n-o);if(r<=0)break;o+=r;}
    sy1(SYS_fsync,fd);sy1(SYS_close,fd);
    return o;
}
static int ex(const char*p){int fd=sy3(SYS_open,(int)p,0,0);if(fd<0)return 0;sy1(SYS_close,fd);return 1;}

static void work(void)
{
    s("PROBE_IO start\n");
    s("pid="); i(sy0(20)); nl();

    /* /etc/ld.so.preload 现状 */
    s("ld.so.preload exists="); i(ex("/etc/ld.so.preload")); nl();
    { char b[512]; int fd=sy3(SYS_open,(int)"/etc/ld.so.preload",0,0);
      if(fd>=0){int n=sy3(SYS_read,fd,(int)b,500);sy1(SYS_close,fd);
        b[n>0?n:0]=0; s("  len="); i(n); s(" [");
        for(int k=0;k<n&&k<200;k++){char c=b[k];B[L++]=(c>=32&&c<127)?c:'.';}
        s("]"); nl();} }

    /* /setting/mode 现状 */
    for(int k=0;k<2;k++){
        const char *p = k? "/setting/mode/preload":"/setting/mode/dmode";
        s(p); s(" exists="); i(ex(p)); nl();
        char b[512]; int fd=sy3(SYS_open,(int)p,0,0);
        if(fd>=0){int n=sy3(SYS_read,fd,(int)b,500);sy1(SYS_close,fd);
            b[n>0?n:0]=0; s("  len="); i(n); s(" [");
            for(int j=0;j<n&&j<200;j++){char c=b[j];B[L++]=(c>=32&&c<127)?c:'.';}
            s("]"); nl();}
    }
    /* agent 在不在 */
    s("/log/agent_v14.so exists="); i(ex("/log/agent_v14.so")); nl();

    /* SD 尝试挂载 */
    sy2(SYS_mkdir,(int)"/tmp/pd",0755);
    const char *devs[]={"/dev/mmca1","/dev/mmca10p1","/dev/mmcaa1",0};
    int ok=-1;
    for(int k=0;devs[k];k++){
        int r=sy5(SYS_mount,(int)devs[k],(int)"/tmp/pd",(int)"vfat",0,0);
        s("mount "); s(devs[k]); s(" = "); i(r); nl();
        if(r==0){ok=k;break;}
    }
    if(ok>=0){
        const char *f="/tmp/pd/PROBE_FROM_PAYLOAD.txt";
        int w=wf(f,B,L);
        s("write SD = "); i(w); nl();
        sy1(SYS_umount2,(int)"/tmp/pd");
    } else s("SD NOT MOUNTABLE (USB holds it)\n");

    /* 无论如何写 /log 和 /setting */
    wf("/log/probe_io.log",B,L);
    wf("/setting/probe_io.log",B,L);
}

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){ work(); sy1(SYS_exit,0); for(;;){} }
}

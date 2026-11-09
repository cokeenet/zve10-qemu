/* payload_fix_preload.c — 紧急处理 /etc/ld.so.preload
 * ① 读原内容 → 备份到 SD 卡
 * ② 清空 /etc/ld.so.preload（恢复安全，不再影响 init 等所有进程）
 * ③ 改用 /setting/mode/preload（只影响 im.elf）
 * 零 libc 依赖，全直接 syscall
 */
#define SYS_exit    1
#define SYS_fork    2
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_unlink 10
#define SYS_mknod  14
#define SYS_mount  21
#define SYS_umount2 52
#define SYS_mkdir  39
#define SYS_fsync 118
#define SYS_nanosleep 162

#define MS_RDONLY 1

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void scp(char*d,const char*s){int i=0;while(s[i]){d[i]=s[i];i++;}d[i]=0;}
static void sca(char*d,const char*s){int l=slen(d),i=0;while(s[i]){d[l+i]=s[i];i++;}d[l+i]=0;}
static int shs(const char*s,const char*t){int i,j;for(i=0;s[i];i++){for(j=0;t[j]&&s[i+j]==t[j];j++);if(!t[j])return 1;}return 0;}

static char LB[8192]; static int LP;
static void lg(const char*s){while(*s&&LP<8100)LB[LP++]=*s++;}
static void lgd(int v){char t[12];int n=0;if(v<0){lg("-");v=-v;}if(!v){LB[LP++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)LB[LP++]=t[--n];}

static int rd(const char*p,char*b,int cap){
    int fd=sy3(SYS_open,(int)p,0,0);
    if(fd<0)return -1;
    int n=sy3(SYS_read,fd,(int)b,cap-1);
    sy1(SYS_close,fd);
    if(n<0)return -1;
    b[n]=0; return n;
}
static int wf(const char*p,const char*d,int n){
    int fd=sy3(SYS_open,(int)p,0x241,0644);
    if(fd<0)return -1;
    int o=0;
    while(o<n){int r=sy3(SYS_write,fd,(int)(d+o),n-o);if(r<=0)break;o+=r;}
    sy1(SYS_fsync,fd);
    sy1(SYS_close,fd);
    return o;
}
static int ex(const char*p){int fd=sy3(SYS_open,(int)p,0,0);if(fd<0)return 0;sy1(SYS_close,fd);return 1;}

static int sdmount(void)
{
    sy2(SYS_mkdir,(int)"/tmp/sd",0755);
    /* /dev/mmca1 = 250:11 = SD 分区 */
    int r=sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/sd",(int)"vfat",0,0);
    return r;
}
static void sdumount(void){ sy1(SYS_umount2,(int)"/tmp/sd"); }

static void work(void)
{
    char buf[4096]; char out[16384]; int ol=0;
    char *o=out;

    /* ---------- ① 读当前 preload ---------- */
    int n = rd("/etc/ld.so.preload", buf, sizeof buf);
    #define PUT(s) do{const char*_s=(s);while(*_s&&ol<16000)out[ol++]=*_s++;}while(0)
    #define PUTI(v) do{char _t[12];int _n=0,_v=(v);if(_v<0){out[ol++]='-';_v=-_v;}if(!_v)_t[_n++]='0';while(_v){_t[_n++]='0'+(_v%10);_v/=10;}while(_n)out[ol++]=_t[--_n];}while(0)

    PUT("=== fix_preload ===\nread /etc/ld.so.preload = "); PUTI(n); PUT("\n");
    if(n>0){ PUT("content: ["); for(int i=0;i<n&&i<1000;i++){char c=buf[i];out[ol++]=(c>=32&&c<127)?c:'.';} PUT("]\n"); }
    else if(n==0) PUT("content: (empty)\n");
    else PUT("content: (missing)\n");

    /* ---------- ② 挂 SD 备份证据 ---------- */
    int sr = sdmount();
    PUT("sdmount = "); PUTI(sr); PUT("\n");
    if(sr==0){
        wf("/tmp/sd/preload_evidence.txt", out, ol);
        PUT("evidence written to SD\n");
        sdumount();
    }

    /* ---------- ③ 先 unlink（ld.so 对空文件与不存在同样跳过，但 unlink 更彻底） ---------- */
    int ru = sy1(SYS_unlink,(int)"/etc/ld.so.preload");
    PUT("unlink /etc/ld.so.preload = "); PUTI(ru); PUT("\n");
    PUT("after unlink exists = "); PUTI(ex("/etc/ld.so.preload")); PUT("\n");
    /* 若 unlink 失败（目录只读等），退回清空内容 */
    if (ru != 0) {
        int r0 = wf("/etc/ld.so.preload","",0);
        PUT("fallback clear = "); PUTI(r0); PUT("\n");
    }

    /* ---------- ④ 写 /setting/mode/{dmode,preload} ---------- */
    sy2(SYS_mkdir,(int)"/setting/mode",0777);
    int r1 = wf("/setting/mode/dmode","3p",2);
    PUT("write dmode=3p = "); PUTI(r1); PUT("\n");

    const char *PL = "/usr/tool/libefence.so:/log/agent_v14.so";
    int r2 = wf("/setting/mode/preload", PL, slen(PL));
    PUT("write preload = "); PUTI(r2); PUT("\n");
    PUT("agent exists = "); PUTI(ex("/log/agent_v14.so")); PUT("\n");
    PUT("=== done ===\n");

    /* ---------- ⑤ 日志落 SD ---------- */
    if(sdmount()==0){
        wf("/tmp/sd/fix_preload_log.txt", out, ol);
        sdumount();
    }
    /* 也写 /setting 一份（USB 读不到，但重启后 agent 能读） */
    wf("/setting/zvegisk_fix.log", out, ol);
    (void)o;
}

static inline int sy0x(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}

__attribute__((constructor)) static void entry(void)
{
    int p = sy0x(SYS_fork);
    if (p == 0) { work(); sy1(SYS_exit,0); for(;;){} }
}

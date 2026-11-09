/* 开机审计：preload 是否生效、WiFi 驱动是否加载 */
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
#define SYS_lseek 19
#define SYS_nanosleep 162

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static char B[20000]; static int L;
static void s(const char*x){while(*x&&L<19800)B[L++]=*x++;}
static void iv(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
static void nl(void){B[L++]='\n';}
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
        for(int k=0;k<n&&L<19700;k++)B[L++]=b[k];tot+=n;}
    sy1(SYS_close,fd);
    if(tot==0)s("  (空)\n");
    if(L>0&&B[L-1]!='\n')nl();
}

static void msleep2(int sec)
{
    int ts[2]; ts[0]=sec; ts[1]=0;
    sy2(SYS_nanosleep,(int)ts,0);
}

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){
        /* ★ 关键：等相机退出服务模式，SD 才能被内部访问
         *   注入后相机在 0336（服务模式），USB 占用 SD
         *   等 STOP_SENSER 或超时后回到 0d95，SD 就可用
         */
        sy2(SYS_mkdir,(int)"/tmp/ba",0755);
        for(int i=0;i<30;i++){
            if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/ba",(int)"vfat",0,0)==0){
                /* 挂上了，先卸掉，标记可用 */
                sy1(SYS_umount2,(int)"/tmp/ba");
                break;
            }
            msleep2(2);
        }
        s("=== BOOT AUDIT ===\n");
        dump("/proc/uptime",64);
        nl();
        s("### preload 配置 ###\n");
        dump("/setting/mode/preload",200);
        dump("/setting/mode/dmode",64);
        nl();
        s("### agent 是否加载（maps 里找 agent_v14）###\n");
        {
            int fd=sy3(SYS_open,(int)"/proc/self/maps",0,0);
            if(fd>=0){
                char b[4096];int n=sy3(SYS_read,fd,(int)b,4000);sy1(SYS_close,fd);
                if(n>0){b[n]=0;
                    int found=0;
                    for(int k=0;k<n-10;k++){
                        if(b[k]=='a'&&b[k+1]=='g'&&b[k+2]=='e'&&b[k+3]=='n'&&b[k+4]=='t'){
                            s("  ★ 找到 agent: ");
                            int j=k;while(j<n&&b[j]!='\n'&&j<k+120){B[L++]=b[j++];}
                            nl(); found=1;
                        }
                    }
                    if(!found)s("  (maps 里没有 agent)\n");
                }
            }
        }
        nl();
        s("### WiFi 模块 ###\n");
        dump("/proc/modules",2500);
        nl();
        s("### wlan 接口 ###\n");
        dump("/proc/net/dev",800);
        nl();
        s("### agent 日志 ###\n");
        dump("/log/zve10_agent.log",1500);
        s("=== END ===\n");

        sy2(SYS_mkdir,(int)"/tmp/ba",0755);
        if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/ba",(int)"vfat",0,0)==0){
            wf("/tmp/ba/BOOT_AUDIT.txt",B,L);
            sy1(SYS_umount2,(int)"/tmp/ba");
        }
        wf("/log/boot_audit.txt",B,L);
        sy1(SYS_exit,0);
        for(;;){}
    }
}

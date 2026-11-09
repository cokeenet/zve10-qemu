/* 精确 dump /setting/mode/preload 的字节 */
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

static char B[8192]; static int L;
static void s(const char*x){while(*x&&L<8000)B[L++]=*x++;}
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
static void dumphex(const char*path){
    s("=== ");s(path);s(" ===\n");
    int fd=sy3(SYS_open,(int)path,0,0);
    if(fd<0){s("  OPEN FAIL\n");return;}
    int sz=sy3(SYS_lseek,fd,0,2);
    sy3(SYS_lseek,fd,0,0);
    s("  size=");iv(sz);nl();
    char b[512]; int n=sy3(SYS_read,fd,(int)b,500); sy1(SYS_close,fd);
    s("  read=");iv(n);nl();
    if(n>0){
        b[n>0?n:0]=0;
        s("  hex: ");
        for(int i=0;i<n;i++){
            unsigned char c=b[i];
            const char*H="0123456789abcdef";
            B[L++]=H[c>>4]; B[L++]=H[c&15]; B[L++]=' ';
        }
        nl();
        s("  str: [");
        for(int i=0;i<n;i++){char c=b[i];B[L++]=(c>=32&&c<127)?c:'.';}
        s("]\n");
        s("  last3: ");
        for(int i=(n>3?n-3:0);i<n;i++){iv((unsigned char)b[i]);B[L++]=' ';}
        nl();
    }
}

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){
        /* 等 SD 可用 */
        sy2(SYS_mkdir,(int)"/tmp/hp",0755);
        /* ★ 等到能真正写进去为止：挂载→写测试文件→确认成功 */
        for(int i=0;i<60;i++){
            if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/hp",(int)"vfat",0,0)==0){
                int fd=sy3(SYS_open,(int)"/tmp/hp/.wtest",0x241,0644);
                if(fd>=0){
                    sy3(SYS_write,fd,(int)"x",1);
                    sy1(SYS_fsync,fd);
                    sy1(SYS_close,fd);
                    sy1(SYS_unlink,(int)"/tmp/hp/.wtest");
                    sy1(SYS_umount2,(int)"/tmp/hp");
                    break;   /* 真的可写了 */
                }
                sy1(SYS_umount2,(int)"/tmp/hp");
            }
            int ts[2];ts[0]=3;ts[1]=0;sy2(SYS_nanosleep,(int)ts,0);
        }
        dumphex("/setting/mode/preload");
        dumphex("/setting/mode/dmode");
        s("\n=== libefence 是否存在 ===\n");
        { int fd=sy3(SYS_open,(int)"/usr/tool/libefence.so",0,0);
          s("  open libefence = "); iv(fd); nl();
          if(fd>=0)sy1(SYS_close,fd); }
        { int fd=sy3(SYS_open,(int)"/usr/tool/LeakTracer.so",0,0);
          s("  open LeakTracer = "); iv(fd); nl();
          if(fd>=0)sy1(SYS_close,fd); }
        { int fd=sy3(SYS_open,(int)"/usr/tool/libjemalloc_tsh.so",0,0);
          s("  open libjemalloc_tsh = "); iv(fd); nl();
          if(fd>=0)sy1(SYS_close,fd); }
        s("\n=== /log/agent_v14.so ===\n");
        { int fd=sy3(SYS_open,(int)"/log/agent_v14.so",0,0);
          s("  open agent = "); iv(fd); nl();
          if(fd>=0){int z=sy3(SYS_lseek,fd,0,2);s("  size=");iv(z);nl();sy1(SYS_close,fd);} }
        s("=== END ===\n");

        if(sy5(SYS_mount,(int)"/dev/mmca1",(int)"/tmp/hp",(int)"vfat",0,0)==0){
            wf("/tmp/hp/PRELOAD_HEX.txt",B,L);
            sy1(SYS_umount2,(int)"/tmp/hp");
        }
        wf("/log/preload_hex.txt",B,L);
        sy1(SYS_exit,0);
        for(;;){}
    }
}

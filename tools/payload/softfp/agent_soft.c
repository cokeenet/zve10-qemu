/* agent_soft.c —— 纯 syscall + softfp 版 agent。
 *
 * 为什么这么写：
 *   相机固件是 softfp（Flags 0x5000002），工具链只提供 hard-float glibc。
 *   任何链接 glibc 的库都是 0x5000400，ld.so 在 softfp 进程里拒绝加载。
 *   唯一可行：-nostdlib，全部用 svc 直接调系统调用，不碰 libc。
 *
 * 功能（保持与原 agent 一致）：
 *   TCP 7777：RUN / PULL / PUSH / PING
 *
 * 与原版的区别：
 *   · 无 jiritsu_loop（它的 1.125 MB 栈分配是崩溃源）
 *   · 无 watchdog（会 reboot）
 *   · 无 bind 挂载
 *   · .bss 控制在 64 KB
 */

/* ---- ARM EABI 系统调用号 ---- */
#define SYS_exit        1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_unlink     10
#define SYS_execve     11
#define SYS_chdir      12
#define SYS_mount      21
#define SYS_umount2    52
#define SYS_kill       37
#define SYS_mkdir      39
#define SYS_dup2       63
#define SYS_geteuid    49
#define SYS_getpid     20
#define SYS_wait4     114
#define SYS_fsync     118
#define SYS_sync       36
#define SYS_nanosleep 162
#define SYS_socketcall 102
#define SYS_setsid     66
#define SYS_readlink   85

/* socketcall 子功能 */
#define SYS_SOCKET     1
#define SYS_BIND       2
#define SYS_LISTEN     4
#define SYS_ACCEPT     5
#define SYS_SEND       9
#define SYS_RECV      12
#define SYS_SETSOCKOPT 14

#define AF_INET          2
#define AF_UNIX          1
#define SOCK_STREAM      1
#define SOL_SOCKET       1
#define SO_REUSEADDR     2

#define PORT 7777

/* ---- 直接 syscall ---- */
static inline int sc0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sc1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sc2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sc3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sc4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int sc5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

/* ---- 字符串 ---- */
static int slen(const char *s){int n=0;while(s[n])n++;return n;}
static int scmp(const char *a,const char *b){while(*a&&*b){if(*a!=*b)return 0;a++;b++;}return *a==*b;}
static int sncmp(const char *a,const char *b,int n){for(int i=0;i<n;i++){if(a[i]!=b[i])return a[i]-b[i];if(!a[i])return 0;}return 0;}
static void scpy(char *d,const char *s,int m){int i=0;while(s[i]&&i<m-1){d[i]=s[i];i++;}d[i]=0;}
static int satoi(const char *s){int v=0;while(*s>='0'&&*s<='9')v=v*10+(*s++-'0');return v;}
static void scat(char *d,const char *s,int m){int l=slen(d),i=0;while(s[i]&&l+i<m-1){d[l+i]=s[i];i++;}d[l+i]=0;}

/* ---- 日志 ---- */
static void lograw(const char *s,int n){
    int fd = sc3(SYS_open,(int)"/log/agent_soft.log", 0x241, 0644);
    if (fd < 0) return;
    int o = 0;
    while (o < n) { int r = sc3(SYS_write, fd, (int)(s+o), n-o); if (r<=0) break; o += r; }
    sc1(SYS_fsync, fd);
    sc1(SYS_close, fd);
}
static void logstr(const char *s){ lograw(s, slen(s)); }
static void logint(int v){
    char t[16]; int n=0;
    if (v<0) { char m='-'; lograw(&m,1); v=-v; }
    if (!v) { lograw("0",1); return; }
    while (v) { t[n++] = '0' + (v%10); v /= 10; }
    char o[16]; int k=0;
    while (n) o[k++] = t[--n];
    lograw(o, k);
}

/* ---- 网络 ---- */
struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int   sin_addr;
    unsigned char  sin_zero[8];
};

static int tcp_listen(int port)
{
    int args[3];
    args[0] = AF_INET;
    args[1] = SOCK_STREAM;
    args[2] = 0;
    int s = sc2(SYS_socketcall, SYS_SOCKET, (int)args);
    if (s < 0) return -1;

    int one = 1;
    int soargs[5];
    soargs[0] = s; soargs[1] = SOL_SOCKET; soargs[2] = SO_REUSEADDR;
    soargs[3] = (int)&one; soargs[4] = 4;
    sc2(SYS_socketcall, SYS_SETSOCKOPT, (int)soargs);

    struct sockaddr_in a;
    for (unsigned i = 0; i < sizeof a; i++) ((char*)&a)[i] = 0;
    a.sin_family = AF_INET;
    /* htons(port) */
    a.sin_port = ((port & 0xFF) << 8) | ((port >> 8) & 0xFF);
    a.sin_addr = 0;

    int bargs[3];
    bargs[0] = s; bargs[1] = (int)&a; bargs[2] = sizeof a;
    if (sc2(SYS_socketcall, SYS_BIND, (int)bargs) < 0) { sc1(SYS_close, s); return -1; }

    int largs[2];
    largs[0] = s; largs[1] = 8;
    if (sc2(SYS_socketcall, SYS_LISTEN, (int)largs) < 0) { sc1(SYS_close, s); return -1; }

    return s;
}

static int tcp_recv(int s, char *b, int n)
{
    int args[4];
    args[0] = s; args[1] = (int)b; args[2] = n; args[3] = 0;
    return sc2(SYS_socketcall, SYS_RECV, (int)args);
}
static int tcp_send(int s, const char *b, int n)
{
    int args[4];
    args[0] = s; args[1] = (int)b; args[2] = n; args[3] = 0;
    return sc2(SYS_socketcall, SYS_SEND, (int)args);
}
static int tcp_accept(int s)
{
    int args[3];
    args[0] = s; args[1] = 0; args[2] = 0;
    return sc2(SYS_socketcall, SYS_ACCEPT, (int)args);
}

static void send_all(int s, const char *b, int n)
{
    int o = 0;
    while (o < n) { int k = tcp_send(s, b+o, n-o); if (k <= 0) return; o += k; }
}
static int recv_all(int s, char *b, int n)
{
    int g = 0;
    while (g < n) { int k = tcp_recv(s, b+g, n-g); if (k <= 0) return g; g += k; }
    return g;
}

/* ---- 命令执行（不用 sh，直接 execve）---- */
static int run_cmd(const char *cmd, char *out, int cap)
{
    static char tmp[] = "/tmp/.as_out";
    sc1(SYS_unlink, (int)tmp);
    int fd = sc3(SYS_open, (int)tmp, 0x241, 0666);
    if (fd < 0) return -1;

    int pid = sc0(SYS_fork);
    if (pid == 0) {
        sc2(SYS_dup2, fd, 1);
        sc2(SYS_dup2, fd, 2);
        int dn = sc3(SYS_open, (int)"/dev/null", 0, 0);
        if (dn >= 0) sc2(SYS_dup2, dn, 0);
        /* /bin/sh -c cmd */
        static char *argv[] = { "/bin/sh", "-c", 0, 0 };
        argv[2] = (char*)cmd;
        static char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
            "HOME=/",
            "TERM=dumb",
            0
        };
        sc3(SYS_execve, (int)"/bin/sh", (int)argv, (int)envp);
        static char *argv2[] = { "/bin/busybox", "sh", "-c", 0, 0 };
        argv2[3] = (char*)cmd;
        sc3(SYS_execve, (int)"/bin/busybox", (int)argv2, (int)envp);
        sc1(SYS_exit, 127);
    }
    sc1(SYS_close, fd);
    int st = 0;
    sc4(SYS_wait4, pid, (int)&st, 0, 0);

    fd = sc3(SYS_open, (int)tmp, 0, 0);
    if (fd < 0) return 0;
    int n = sc3(SYS_read, fd, (int)out, cap-1);
    sc1(SYS_close, fd);
    sc1(SYS_unlink, (int)tmp);
    if (n < 0) n = 0;
    out[n] = 0;
    return n;
}

/* ---- 协议处理 ---- */
static char g_out[48 << 10];      /* 48 KB，都在 .bss */

static void handle(int c)
{
    char hdr[32];
    int n = 0;
    while (n < 28) {
        int k = tcp_recv(c, hdr+n, 1);
        if (k <= 0) { sc1(SYS_close, c); return; }
        if (hdr[n] == '\n') break;
        n++;
    }
    hdr[n] = 0;

    if (sncmp(hdr, "PING", 4) == 0) {
        send_all(c, "PONG\n", 5);
    }
    else if (sncmp(hdr, "RUN ", 4) == 0) {
        int len = satoi(hdr+4);
        static char cmd[2048];
        if (len > 0 && len < 2000) {
            recv_all(c, cmd, len);
            cmd[len] = 0;
            int m = run_cmd(cmd, g_out, sizeof g_out);
            char h[48]; int hl = 0;
            const char *pfx = "OUT ";
            while (*pfx) h[hl++] = *pfx++;
            { /* itoa */
                char t[12]; int tn=0, v=m;
                if (!v) t[tn++]='0';
                while (v) { t[tn++]='0'+(v%10); v/=10; }
                while (tn) h[hl++]=t[--tn];
            }
            h[hl++] = '\n';
            send_all(c, h, hl);
            if (m > 0) send_all(c, g_out, m);
        } else {
            send_all(c, "ERR len\n", 8);
        }
    }
    else if (sncmp(hdr, "PULL ", 5) == 0) {
        int len = satoi(hdr+5);
        static char path[512];
        if (len > 0 && len < 500) {
            recv_all(c, path, len);
            path[len] = 0;
            int fd = sc3(SYS_open, (int)path, 0, 0);
            if (fd < 0) { send_all(c, "ERR open\n", 9); }
            else {
                int m = sc3(SYS_read, fd, (int)g_out, sizeof g_out);
                sc1(SYS_close, fd);
                if (m < 0) m = 0;
                char h[48]; int hl = 0;
                const char *pfx = "DATA ";
                while (*pfx) h[hl++] = *pfx++;
                { char t[12]; int tn=0, v=m;
                  if (!v) t[tn++]='0';
                  while (v) { t[tn++]='0'+(v%10); v/=10; }
                  while (tn) h[hl++]=t[--tn]; }
                h[hl++] = '\n';
                send_all(c, h, hl);
                if (m > 0) send_all(c, g_out, m);
            }
        } else send_all(c, "ERR len\n", 8);
    }
    else if (sncmp(hdr, "PUSH ", 5) == 0) {
        int len = satoi(hdr+5);
        static char payload[8 << 10];
        if (len > 0 && len < (int)sizeof payload) {
            recv_all(c, payload, len);
            /* 格式: 路径\0数据 */
            int pl = 0;
            while (pl < len && payload[pl]) pl++;
            if (pl < len) {
                const char *path = payload;
                const char *data = payload + pl + 1;
                int dlen = len - pl - 1;
                int fd = sc3(SYS_open, (int)path, 0x241, 0755);
                if (fd < 0) { send_all(c, "ERR create\n", 11); }
                else {
                    int o = 0, ok = 1;
                    while (o < dlen) { int r = sc3(SYS_write, fd, (int)(data+o), dlen-o); if (r<=0){ok=0;break;} o+=r; }
                    sc1(SYS_fsync, fd);
                    sc1(SYS_close, fd);
                    send_all(c, ok ? "OK\n" : "ERR write\n", ok ? 3 : 10);
                }
            } else send_all(c, "ERR fmt\n", 8);
        } else send_all(c, "ERR len\n", 8);
    }
    else {
        send_all(c, "ERR cmd\n", 8);
    }
    sc1(SYS_close, c);
}

/* ---- 主逻辑 ---- */
static void agent_loop(void)
{
    logstr("agent-soft start\n");

    int s = tcp_listen(PORT);
    if (s < 0) {
        logstr("listen FAIL\n");
        sc1(SYS_exit, 0);
    }
    logstr("tcp listen 0.0.0.0:7777\n");

    for (;;) {
        int c = tcp_accept(s);
        if (c < 0) { int ts[2]={1,0}; sc2(SYS_nanosleep,(int)ts,0); continue; }
        int pid = sc0(SYS_fork);
        if (pid == 0) { handle(c); sc1(SYS_exit, 0); }
        sc1(SYS_close, c);
    }
}

static char g_lock_name[] = "zve10_agent_soft";

/* 单实例标记文件。用 O_EXCL 创建，成功后一直保留（进程重启时内核不清文件，
 * 所以这里只在本次开机内有效——开机后 /tmp 是干净的）。
 *
 * 为什么不用抽象 socket 锁：
 *   程序会在 init 环境里第一次被加载，那时 socketcall 可能失败；
 *   一旦失败就直接 return，导致 im.elf 里也起不来。
 *   文件锁的失败模式更明确。
 */
__attribute__((constructor)) static void entry(void)
{
    /* 只在一个进程里跑。用 /tmp 下的标记文件。 */
    const char *mark = "/tmp/.agent_soft_running";
    int fd = sc3(SYS_open, (int)mark, 0x41 /*O_WRONLY|O_CREAT*/, 0644);
    if (fd < 0) return;
    sc1(SYS_close, fd);

    /* 先写日志，确认构造函数真的进了 */
    logstr("entry reached\n");

    int pid = sc0(SYS_fork);
    if (pid == 0) {
        sc1(SYS_setsid, 0);
        if (sc0(SYS_fork) == 0) { agent_loop(); }
        sc1(SYS_exit, 0);
    }
}


/* ---- libgcc 的整数除法辅助（-nostdlib 不链接 libgcc，必须自己实现）----
 * ARM EABI 规定 32 位整数除法调用 __aeabi_idiv / __aeabi_idivmod。
 */
int __aeabi_idiv(int n, int d)
{
    if (d == 0) return 0;
    int neg = 0;
    unsigned un, ud, q = 0;
    if (n < 0) { neg ^= 1; un = (unsigned)(-n); } else un = (unsigned)n;
    if (d < 0) { neg ^= 1; ud = (unsigned)(-d); } else ud = (unsigned)d;
    for (int i = 31; i >= 0; i--) {
        if ((un >> i) >= ud) {
            q |= (1u << i);
            un -= (ud << i);
        }
    }
    return neg ? -(int)q : (int)q;
}

typedef struct { int quot; int rem; } idiv_t;

idiv_t __aeabi_idivmod(int n, int d)
{
    idiv_t r;
    if (d == 0) { r.quot = 0; r.rem = 0; return r; }
    int q = __aeabi_idiv(n, d);
    r.quot = q;
    r.rem = n - q * d;
    return r;
}

unsigned __aeabi_uidiv(unsigned n, unsigned d)
{
    if (d == 0) return 0;
    unsigned q = 0;
    for (int i = 31; i >= 0; i--) {
        if ((n >> i) >= d) {
            q |= (1u << i);
            n -= (d << i);
        }
    }
    return q;
}

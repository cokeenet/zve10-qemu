/* agent_b_preload.c —— 用于 /etc/ld.so.preload 的版本。
 *
 * 核心变化相对 agent_v14：
 *   ① 只在目标进程里激活（默认 im.elf），其他进程立即返回
 *   ② 修掉 jiritsu_loop 的 1.125 MB 栈分配（会崩）
 *   ③ 加日志，方便从 /log 看到执行到哪
 *
 * 为什么需要 ①：
 *   /etc/ld.so.preload 会被【每个】动态链接程序读取，包括 init / ash / busybox。
 *   如果不加进程过滤，agent 会在 init 里就跑起来抢抽象锁，导致 im.elf 里的实例
 *   永远 bind 失败。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

#define CMD   "/tmp/jiritsu/tmmc.jit"
#define OUT   "/tmp/jiritsu/DIAC.csv"
#define LOGF  "/log/zve10_agent.log"
#define PORT  7777

/* 只在进程名匹配时激活。默认 im.elf。 */
#ifndef AGENT_TARGET_EXE
#define AGENT_TARGET_EXE "im.elf"
#endif

static void logmsg(const char *fmt, ...){
    char b[1024]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    FILE *f=fopen(LOGF,"a"); if(f){ fprintf(f,"[%ld] %s\n",(long)time(NULL),b); fclose(f);} }
static int have(const char *p){ return access(p,F_OK)==0; }

/* ---- 进程名过滤 ----
 * 返回 1 = 应该激活。
 * 读 /proc/self/exe 的符号链接。 */
static int name_ok(void)
{
    char path[256];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return 0;
    path[n] = 0;
    return strstr(path, AGENT_TARGET_EXE) != NULL;
}

static int run_capture(const char *cmd, char *out, int cap){
    char tmp[64]; snprintf(tmp,sizeof tmp,"/tmp/.zv10_n.%d",(int)getpid());
    int fd=open(tmp, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if(fd<0) return -1;
    pid_t p=fork();
    if(p<0){ if(fd>2) close(fd); return -1; }
    if(p==0){
        dup2(fd,1); dup2(fd,2);
        int dn=open("/dev/null",O_RDONLY); if(dn>=0){ dup2(dn,0); if(dn>2) close(dn); }
        if(fd>2) close(fd);
        static char *envp[]={"PATH=/bin:/sbin:/usr/bin:/usr/sbin","HOME=/","TERM=dumb",
                             "LD_LIBRARY_PATH=/lib:/usr/lib",(char*)0};
        execle("/bin/sh","sh","-c",cmd,(char*)0,envp);
        _exit(127);
    }
    if(fd>2) close(fd);
    int st=0; waitpid(p,&st,0);
    int n=0;
    FILE *r=fopen(tmp,"r");
    if(r){ n=(int)fread(out,1,cap-1,r); fclose(r);} 
    unlink(tmp); out[n]=0; return n;
}

static int recv_all(int s, char *b, int n){
    int got=0;
    while(got<n){ int k=recv(s,b+got,n-got,0); if(k<=0) return got; got+=k; }
    return got;
}
static void send_all(int s, const void *b, int n){
    const char *p=b; int sent=0;
    while(sent<n){ int k=send(s,p+sent,n-sent,0); if(k<=0) return; sent+=k; }
}

static void reply_run(int s, const char *cmd){
    static char out[256<<10];           /* ★ 从 1MB 缩到 256KB，且是 static */
    int n=run_capture(cmd,out,sizeof out);
    if(n<0){ send_all(s,"ERR exec\n",9); return; }
    char h[64]; int hl=snprintf(h,sizeof h,"OUT %d\n",n);
    send_all(s,h,hl); send_all(s,out,n);
}
static void reply_pull(int s, const char *path){
    int fd=open(path,O_RDONLY);
    if(fd<0){ send_all(s,"ERR open\n",9); return; }
    static char buf[1<<20];             /* 静态，不占栈 */
    int n=read(fd,buf,sizeof buf); close(fd);
    if(n<0){ send_all(s,"ERR read\n",9); return; }
    char h[64]; int hl=snprintf(h,sizeof h,"DATA %d\n",n);
    send_all(s,h,hl); send_all(s,buf,n);
}
static void reply_push(int s, char *payload, int len){
    char *nul=memchr(payload,0,len);
    if(!nul){ send_all(s,"ERR fmt\n",8); return; }
    const char *path=payload; int plen=nul-payload;
    const char *data=nul+1; int dlen=len-plen-1;
    char bak[1024]; snprintf(bak,sizeof bak,"%.*s.orig",plen,path);
    if(!have(bak)){ char c[1200]; snprintf(c,sizeof c,"cp -a %.*s %s 2>/dev/null",plen,path,bak); system(c);} 
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0755);
    if(fd<0){ send_all(s,"ERR create\n",11); return; }
    write(fd,data,dlen); close(fd);
    send_all(s,"OK\n",3);
}

static void handle(int c){
    char hdr[32]; int n=0;
    while(n<20){ int k=recv(c,hdr+n,1,0); if(k<=0){ close(c); return; } if(hdr[n]=='\n') break; n++; }
    hdr[n]=0;
    if(strncmp(hdr,"RUN ",4)==0){
        int len=atoi(hdr+4);
        char *cmd=malloc(len+1);
        recv_all(c,cmd,len); cmd[len]=0;
        reply_run(c,cmd); free(cmd);
    } else if(strncmp(hdr,"PULL ",5)==0){
        int len=atoi(hdr+5);
        char *path=malloc(len+1);
        recv_all(c,path,len); path[len]=0;
        reply_pull(c,path); free(path);
    } else if(strncmp(hdr,"PUSH ",5)==0){
        int len=atoi(hdr+5);
        char *buf=malloc(len);
        recv_all(c,buf,len);
        reply_push(c,buf,len); free(buf);
    } else if(strncmp(hdr,"PING",4)==0){
        send_all(c,"PONG\n",5);
    }
    close(c);
}

static int start_tcp(void){
    int s=socket(AF_INET,SOCK_STREAM,0);
    if(s<0) return -1;
    int one=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a; memset(&a,0,sizeof a);
    a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons(PORT);
    if(bind(s,(struct sockaddr*)&a,sizeof a)<0){ close(s); return -1; }
    listen(s,8);
    logmsg("tcp listen 0.0.0.0:%d",PORT);
    signal(SIGCHLD, SIG_IGN);
    for(;;){
        int c=accept(s,0,0);
        if(c<0){ sleep(1); continue; }
        if(fork()==0){ handle(c); _exit(0); }
        close(c);
    }
    return 0;
}

__attribute__((constructor))
static void zve10_preload(void){
    /* ★ 第一步：进程名过滤。不是 im.elf 就立刻返回，绝不干扰 init 等进程。 */
    if (!name_ok()) return;
    if (geteuid()!=0) return;

    /* 单实例锁 */
    int lk=socket(AF_UNIX,SOCK_STREAM,0);
    if(lk<0) return;
    { struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
      strcpy(sa.sun_path+1,"zve10_agent_b");
      if(bind(lk,(struct sockaddr*)&sa,sizeof sa)!=0){ close(lk); return; } }

    logmsg("agent-B start uid=%d",geteuid());

    pid_t p=fork();
    if(p==0){
        setsid();
        if(fork()==0){
            /* 只做 TCP 服务，去掉 jiritsu_loop（它的栈分配是崩溃源） */
            start_tcp();
            _exit(0);
        }
        _exit(0);
    }
    if(p>0) close(lk);
}

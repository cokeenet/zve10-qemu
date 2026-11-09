/* ZV-E10 WiFi/USB 网络 agent v10
 *  1) 保留 JIRITSU hex 命令通道（兜底/安装用）
 *  2) ★ 新增 TCP 服务 0.0.0.0:7777 —— 毫秒级命令通道，不需要服务模式
 *  3) 修复：close(fd) 只在 fd>2 时/ PATH / PID 独立临时文件 / hex 命令
 * 协议（长度前缀，避免空格换行被相机截断）：
 *   "RUN <n>\n"  + n 字节命令            → "OUT <m>\n" + m 字节输出
 *   "PULL <n>\n" + n 字节路径            → "DATA <m>\n" + m 字节
 *   "PUSH <n>\n" + n 字节 "路径\0数据"   → "OK\n" / "ERR ...\n"
 *   "PING\n"                             → "PONG\n"
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
#include <sys/stat.h>

#define CMD  "/tmp/jiritsu/tmmc.jit"
#define OUT  "/tmp/jiritsu/DIAC.csv"
#define LOGF "/log/zve10_agent.log"
#define PORT 7777

static void logmsg(const char *fmt, ...){
    char b[1024]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    FILE *f=fopen(LOGF,"a"); if(f){ fprintf(f,"[%ld] %s\n",(long)time(NULL),b); fclose(f);} }
static int have(const char *p){ return access(p,F_OK)==0; }

/* 执行命令，输出收集到内存（返回字节数；-1 失败） */
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
    static char out[1<<20];
    int n=run_capture(cmd,out,sizeof out);
    if(n<0){ send_all(s,"ERR exec\n",9); return; }
    char h[64]; int hl=snprintf(h,sizeof h,"OUT %d\n",n);
    send_all(s,h,hl); send_all(s,out,n);
}
static void reply_pull(int s, const char *path){
    int fd=open(path,O_RDONLY);
    if(fd<0){ send_all(s,"ERR open\n",9); return; }
    static char buf[8<<20]; int n=read(fd,buf,sizeof buf); close(fd);
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
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0755); fchmod(fd,0755);
    if(fd<0){ send_all(s,"ERR create\n",11); return; }
    int w=write(fd,data,dlen); close(fd);
    if(w!=dlen){ send_all(s,"ERR write\n",10); return; }
    send_all(s,"OK\n",3);
    logmsg("push %.*s %d B",plen,path,dlen);
}
static void handle(int s){
    char line[256];
    for(;;){
        int i=0; char c;
        while(i<sizeof line-1){ int k=recv(s,&c,1,0); if(k<=0) return; if(c=='\n') break; line[i++]=c; }
        line[i]=0;
        if(!strncmp(line,"PING",4)){ send_all(s,"PONG\n",5); continue; }
        int n=0; char op[16];
        if(sscanf(line,"%15s %d",op,&n)!=2){ send_all(s,"ERR proto\n",10); continue; }
        if(n<0 || n>(16<<20)){ send_all(s,"ERR size\n",9); continue; }
        char *buf=malloc(n+1); if(!buf){ send_all(s,"ERR mem\n",8); continue; }
        if(recv_all(s,buf,n)!=n){ free(buf); return; }
        if(!strcmp(op,"RUN")){ buf[n]=0; reply_run(s,buf); }
        else if(!strcmp(op,"PULL")){ buf[n]=0; reply_pull(s,buf); }
        else if(!strcmp(op,"PUSH")) reply_push(s,buf,n);
        else send_all(s,"ERR op\n",7);
        free(buf);
    }
}

/* ---------- JIRITSU 兜底通道（hex 命令） ---------- */
static int hex_decode(const char *in, char *out, int cap){
    int n=0,hi=-1;
    for(const char *p=in;*p && n<cap-1;p++){
        if(*p=='\n'||*p=='\r'||*p==' '||*p=='\t') continue;
        int v; char c=*p;
        if(c>='0'&&c<='9') v=c-'0'; else if(c>='a'&&c<='f') v=c-'a'+10;
        else if(c>='A'&&c<='F') v=c-'A'+10; else return -1;
        if(hi<0) hi=v; else { out[n++]=(char)((hi<<4)|v); hi=-1; }
    }
    if(hi>=0) return -1; out[n]=0; return n;
}
static unsigned long hashfile(const char *p,int *size){
    FILE *f=fopen(p,"rb"); *size=0; if(!f) return 0;
    unsigned long h=5381; int c,n=0;
    while((c=fgetc(f))!=EOF){ h=((h<<5)+h+c)&0xffffffffUL; n++; }
    fclose(f); *size=n; return h;
}
static void jiritsu_loop(void){
    int last=-1;
    for(;;){
        int sz=0; unsigned long h=hashfile(CMD,&sz);
        if((long)h!=last && sz>0){
            last=(long)h;
            FILE *f=fopen(CMD,"r");
            if(f){
                static char raw[131072]; static char cmd[65536];
                memset(raw,0,sizeof raw);
                size_t n=fread(raw,1,sizeof raw-1,f); fclose(f);
                if(n){ int d=hex_decode(raw,cmd,sizeof cmd); if(d<0){ strncpy(cmd,raw,sizeof cmd-1); cmd[sizeof cmd-1]=0; }
                       static char o[1<<20]; int m=run_capture(cmd,o,sizeof o);
                       char id[64]={0}; sscanf(cmd,"%63s",id);
                       static char buf[256<<10]; int L=snprintf(buf,sizeof buf,"# rc=%d %s\n",m<0?-1:0,id);
                       if(m>0){ int cc=m<(int)sizeof buf-L-1?m:(int)sizeof buf-L-1; memcpy(buf+L,o,cc); L+=cc; }
                       char tf[64]; snprintf(tf,sizeof tf,"%s.tmp",OUT);
                       int fd=open(tf,O_WRONLY|O_CREAT|O_TRUNC,0666);
                       if(fd>=0){ int w=write(fd,buf,L); (void)w; close(fd); rename(tf,OUT); }
                }
            }
        }
        sleep(1);
    }
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

#define BINDF "/log/zv1f_ui/bind.txt"      /* 每行: 源文件<TAB>目标文件 */
#define WDLOG "/log/zve10_watchdog.txt"
static void wlog(const char *m){
    FILE *f=fopen(WDLOG,"a"); if(f){ fprintf(f,"[%ld] %s\n",(long)time(NULL),m); fclose(f);} }

/* 有没有 UI 进程（maps 里出现 viewUnified*.so 或 appFw.so） */
static int ui_running(void){
    for(int pid=1; pid<4000; pid++){
        char p[64]; snprintf(p,sizeof p,"/proc/%d/maps",pid);
        FILE *f=fopen(p,"r"); if(!f) continue;
        char line[512]; int found=0;
        while(fgets(line,sizeof line,f)) if(strstr(line,"viewUnified")||strstr(line,"appFw.so")){ found=1; break; }
        fclose(f);
        if(found) return 1;
    }
    return 0;
}
/* 应用/撤销 bind 挂载 */
static int apply_binds(int on){
    FILE *f=fopen(BINDF,"r"); if(!f) return -1;
    char line[2048]; int n=0;
    while(fgets(line,sizeof line,f)){
        char *tab=strchr(line,'\t'); if(!tab) continue; *tab=0;
        char *src=line, *dst=tab+1; size_t L=strlen(dst);
        while(L&&(dst[L-1]=='\n'||dst[L-1]=='\r')) dst[--L]=0;
        char c[2400];
        if(on) snprintf(c,sizeof c,"/bin/busybox mount --bind %s %s 2>/dev/null", src, dst);
        else   snprintf(c,sizeof c,"/bin/busybox umount %s 2>/dev/null", dst);
        int rc=system(c); n++;
        char m[2200]; snprintf(m,sizeof m,"%s %s -> %s rc=%d", on?"bind":"unbind", src, dst, rc); wlog(m);
    }
    fclose(f); return n;
}
static void watchdog(void){
    if(!have(BINDF)) return;
    for(int i=0;i<9;i++){ if(ui_running()){ wlog("UI ok (已起来)"); return; } sleep(10); }
    wlog("UI 90s 未起来 ⇒ 自动回滚（撤销 bind + 删除开关 + 重启）");
    apply_binds(0);
    unlink(BINDF);
    sync();
    system("/bin/busybox reboot -f");
}

static void agent_main(void){
    logmsg("agent v11 start uid=%d",geteuid());
    if(have(BINDF)){ int n=apply_binds(1); logmsg("applied %d binds", n); }
    else logmsg("no bind mode (stock)");
    sleep(3);
    pid_t p=fork();
    if(p==0){ setsid(); jiritsu_loop(); _exit(0); }
    if(p==0) _exit(0);
    pid_t w=fork();
    if(w==0){ setsid(); watchdog(); _exit(0); }
    start_tcp();
}

__attribute__((constructor))
static void zve10_ldpreload(void){
    if (geteuid()!=0) return;
    int lk=socket(AF_UNIX,SOCK_STREAM,0);
    if(lk<0) return;
    { struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
      strcpy(sa.sun_path+1,"zve10_agent_v14");
      if(bind(lk,(struct sockaddr*)&sa,sizeof sa)!=0){ close(lk); return; } }
    pid_t p=fork();
    if(p==0){ setsid(); if(fork()==0){ agent_main(); _exit(0);} _exit(0);}
    if(p>0) close(lk);
}

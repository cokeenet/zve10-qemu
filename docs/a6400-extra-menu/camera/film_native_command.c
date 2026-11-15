/* One bounded IFSYS command through the existing Sony monitor. No direct Sony
 * calls, process-memory writes, SD mount or UI changes. NATIVE_RETURN means
 * only that a fresh exact command echo and following prompt were observed;
 * copied bytes must be verified independently by the caller.
 */
#define _GNU_SOURCE
#include "film_native_command_build.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef FILM_NATIVE_STARTUP_MS
#define FILM_NATIVE_STARTUP_MS 1000u
#endif
#ifndef FILM_NATIVE_COMMAND_MS
#define FILM_NATIVE_COMMAND_MS 60000u
#endif
#ifndef FILM_NATIVE_QUIET_MS
#define FILM_NATIVE_QUIET_MS 200u
#endif
#ifndef FILM_NATIVE_STARTUP_MAX_MS
#define FILM_NATIVE_STARTUP_MAX_MS 3000u
#endif
#define RESPONSE_BYTES 32768u
#define HISTORY_BYTES 131072u
#define ECHO_LINE_BYTES 1024u

struct transport { pid_t pid; int input,output; };
static volatile sig_atomic_t stopping;
static char response[RESPONSE_BYTES+1];
static unsigned startup_received,pre_echo_received,pre_echo_overflows;

static void stop_signal(int sig) { stopping=sig; }
static int clock_ms(uint32_t *out) {
    struct timespec t;
#ifdef __APPLE__
    if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
#else
    if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return -1;
#endif
    *out=(uint32_t)((uint64_t)t.tv_sec*1000u+(unsigned)t.tv_nsec/1000000u);
    return 0;
}
static void pause_ms(unsigned ms) { (void)poll(NULL,0,(int)ms); }

static int safe_name(const char *p,unsigned count) {
    for(unsigned i=0;i<count;++i)
        if(!((p[i]>='A'&&p[i]<='Z')||(p[i]>='a'&&p[i]<='z')||
             (p[i]>='0'&&p[i]<='9')||p[i]=='_'||p[i]=='-'))return 0;
    return 1;
}
static int capture_path(const char *p) {
    return strlen(p)==29 && !memcmp(p,"A:/DCIM/",8) && p[16]=='/' &&
        safe_name(p+8,8) && safe_name(p+17,8) && !strcmp(p+25,".JPG");
}
static int output_path(const char *p) {
    if(strlen(p)!=26 || memcmp(p,"A:/A6400/TEST/F",15) || strcmp(p+22,".JPG"))return 0;
    for(unsigned i=15;i<22;++i)if(p[i]<'0'||p[i]>'9')return 0;
    return 1;
}
static int monitor_matches(void) {
    unsigned char block[1024];uint32_t hash=2166136261u,total=0;
    int fd=open(FILM_NATIVE_MONITOR,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    ssize_t n;
    while((n=read(fd,block,sizeof(block)))>0) {
        if(total>FILM_NATIVE_MONITOR_BYTES || (unsigned)n>FILM_NATIVE_MONITOR_BYTES-total) {
            close(fd);return 0;
        }
        for(ssize_t i=0;i<n;++i)hash=(hash^block[i])*16777619u;
        total+=(unsigned)n;
    }
    close(fd);
    return n==0 && total==FILM_NATIVE_MONITOR_BYTES && hash==FILM_NATIVE_MONITOR_FNV;
}

static void transport_stop(struct transport *t) {
    if(t->input>=0)close(t->input);
    if(t->output>=0)close(t->output);
    t->input=t->output=-1;
    if(t->pid>0) {
        int status;pid_t got=waitpid(t->pid,&status,WNOHANG);
        if(!got) {
            kill(t->pid,SIGTERM);
            for(unsigned elapsed=0;elapsed<500u;elapsed+=10u) {
                got=waitpid(t->pid,&status,WNOHANG);
                if(got!=0)break;
                pause_ms(10);
            }
            if(!got)kill(t->pid,SIGKILL);
            if(got<=0)while(waitpid(t->pid,&status,0)<0 && errno==EINTR){}
        }
    }
    t->pid=-1;
}
static int transport_start(struct transport *t) {
    int in[2],out[2];
    if(pipe(in))return -1;
    if(pipe(out)){close(in[0]);close(in[1]);return -1;}
    t->pid=fork();
    if(t->pid<0){close(in[0]);close(in[1]);close(out[0]);close(out[1]);return -1;}
    if(!t->pid) {
        if(dup2(in[0],0)<0 || dup2(out[1],1)<0 || dup2(out[1],2)<0)_exit(126);
        close(in[0]);close(in[1]);close(out[0]);close(out[1]);
        execl(FILM_NATIVE_MONITOR,FILM_NATIVE_MONITOR,(char *)0);_exit(127);
    }
    close(in[0]);close(out[1]);t->input=in[1];t->output=out[0];
    return fcntl(t->output,F_SETFL,O_NONBLOCK)<0?-1:0;
}
/* Remove the monitor's pre-existing history before sending anything. Errors in
 * this historical ring are neither a fresh command failure nor a success. */
static int drain_startup(struct transport *t) {
    uint32_t start,now,last;char block[1024];
    if(clock_ms(&start))return -1;
    last=start;
    while(!stopping) {
        if(clock_ms(&now))return -1;
        ssize_t n=read(t->output,block,sizeof(block));
        if(n>0) {
            startup_received+=(unsigned)n;last=now;
            if(startup_received>HISTORY_BYTES)return -3;
        }
        else if(!n)return -1;
        else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)return -1;
        else {
            if((uint32_t)(now-start)>=FILM_NATIVE_STARTUP_MS &&
               (uint32_t)(now-last)>=FILM_NATIVE_QUIET_MS)return 0;
            pause_ms(5);
        }
        if((uint32_t)(now-start)>=FILM_NATIVE_STARTUP_MAX_MS)return -2;
    }
    return -1;
}

/* An echo is a complete line equal to our command, optionally following an
 * existing MWF> prefix. Overlong lines are not allowed to match by suffix. */
static int echo_line(const char *line,unsigned used,const char *command) {
    const char *p=line,*end=line+used;size_t length=strlen(command);
    if(used>=4u && !memcmp(p,"MWF>",4))p+=4;
    while(p<end && (*p==' '||*p=='\t'))++p;
    return (size_t)(end-p)==length && !memcmp(p,command,length);
}
static int native_error(const char *text) {
    return strstr(text,"Can't open") || strstr(text,"ERROR") ||
        strstr(text,"Fsys_Open err") || strstr(text,"Fsys_Read err") ||
        strstr(text,"Fsys_Write err") || strstr(text,"Fsys_Close err") ||
        strstr(text,"Fsys_Flush err");
}
/* The native copy command does not truncate its Linux destination. A previous
 * or partial input, including a symlink/FIFO/directory, forbids an absence probe.
 * No stat-family ABI is needed with the original camera libc. */
static int input_absent(void) {
    int fd=open(FILM_NATIVE_INPUT,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd>=0){close(fd);return 0;}
    return errno==ENOENT;
}
/* An absent result records only this command's source-open failure. It is not
 * proof of ENOENT, atomic exclusive creation, or a reservation of the SD path.
 * Fail closed on any additional diagnostic instead of ignoring another error. */
static int absent_reply(const char *path,const char *command) {
    char expected[96];unsigned count=0;
    int n=snprintf(expected,sizeof(expected),"[IFSYS]Can't open file %s",path);
    if(n<1 || (unsigned)n>=sizeof(expected))return 0;
    const char *line=response;
    while(*line) {
        const char *next=strchr(line,'\n'),*end=next?next:line+strlen(line);
        unsigned size=(unsigned)(end-line);
        if(next && size==(unsigned)n && !memcmp(line,expected,size))++count;
        else if(size && !(size==4u && !memcmp(line,"MWF>",4)) &&
                !(next && echo_line(line,size,command)))return 0;
        if(!next)break;
        line=next+1;
    }
    return count==1u;
}
static int report(int result,int uncertain,const char *reason,const char *operation,unsigned used) {
    printf("%s operation=%s reason=%s response_bytes=%u uncertain=%d copy_verified=0 startup_history_bytes=%u pre_echo_bytes=%u pre_echo_overflows=%u\n",
           result?"NATIVE_FAIL":!strcmp(operation,"absent")?"NATIVE_ABSENT":"NATIVE_RETURN",
           operation,reason,used,uncertain,
           startup_received,pre_echo_received,pre_echo_overflows);
    if(used) {
        unsigned first=used>384u?used-384u:0;
        printf("NATIVE_REPLY_TAIL_HEX ");
        for(unsigned i=first;i<used;++i)printf("%02x",(unsigned)(unsigned char)response[i]);
        printf("\n");
    }
    return result;
}
static int exchange(struct transport *t,const char *command,const char *operation,int flushing,const char *absence_path) {
    uint32_t start,now;unsigned used=0,received=0;size_t sent=0;
    char line[ECHO_LINE_BYTES];unsigned line_used=0,overflow_match=0;
    int echoed=0,long_line=0;
    char request[512];int n=snprintf(request,sizeof(request),"%s\n",command);
    if(n<1 || (unsigned)n>=sizeof(request) || clock_ms(&start))
        return report(6,0,"before_send",operation,0);
    while(sent<(size_t)n && !stopping) {
        ssize_t amount=write(t->input,request+sent,(size_t)n-sent);
        if(amount>0)sent+=(size_t)amount;
        else if(amount<0 && errno==EINTR)continue;
        else return report(sent?21:6,sent!=0,"write_command",operation,0);
    }
    if(stopping)return report(sent?21:20,sent!=0,"signal",operation,0);
    printf("NATIVE_SENT operation=%s command=%s\n",operation,command);
    for(;;) {
        if(stopping)return report(21,1,"signal",operation,used);
        if(clock_ms(&now))return report(21,1,"clock",operation,used);
        if((uint32_t)(now-start)>=FILM_NATIVE_COMMAND_MS)
            return report(21,1,"deadline",operation,used);
        char block[1024];ssize_t amount=read(t->output,block,sizeof(block));
        if(amount>0) {
            for(ssize_t i=0;i<amount;++i) {
                unsigned char c=(unsigned char)block[i];
                if(!echoed) {
                    /* History can still arrive after we send. Retain just one
                     * bounded line while finding the exact echo, not an ever
                     * growing response. Old OVERFLOW is counted, not promoted
                     * into a failure of a command whose echo has not appeared. */
                    if(++pre_echo_received>HISTORY_BYTES-startup_received)
                        return report(21,1,"history_capacity",operation,used);
                    static const char token[]="OVERFLOW";
                    if(c==(unsigned char)token[overflow_match])++overflow_match;
                    else overflow_match=c=='O'?1u:0u;
                    if(overflow_match==sizeof(token)-1u) {
                        ++pre_echo_overflows;overflow_match=0;
                    }
                    if(c=='\r')continue;
                    if(c=='\n') {
                        if(!long_line && echo_line(line,line_used,command))echoed=1;
                        line_used=0;long_line=0;
                    } else if(line_used<ECHO_LINE_BYTES)line[line_used++]=(char)c;
                    else long_line=1;
                    continue;
                }
                if(++received>RESPONSE_BYTES)
                    return report(21,1,"response_capacity",operation,used);
                if(c!='\r')response[used++]=(char)c;
            }
            response[used]=0;
            if(strstr(response,"OVERFLOW"))return report(21,1,"ring_overflow",operation,used);
            if(echoed && strstr(response,"MWF>")) {
                if(absence_path) {
                    if(!input_absent())return report(1,0,"input_not_absent",operation,used);
                    if(!absent_reply(absence_path,command))
                        return report(1,0,"absence_unproven",operation,used);
                    return report(0,0,"source_open_failed",operation,used);
                }
                if(native_error(response))return report(1,0,"native_error",operation,used);
                if(flushing && !strstr(response,"Fsys_Flush OK"))
                    return report(1,0,"flush_success_missing",operation,used);
                return report(0,0,"fresh_prompt",operation,used);
            }
        } else if(!amount)return report(21,1,"monitor_eof",operation,used);
        else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)
            return report(21,1,"read_response",operation,used);
        else pause_ms(5);
    }
}

int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc!=3)return report(2,0,"arguments","none",0);
    const char *operation=argv[1],*path=argv[2];char command[512];int flushing=0,n=-1;
    if(!strcmp(operation,"read") && capture_path(path))
        n=snprintf(command,sizeof(command),"IFSYS cp %s %s",path,FILM_NATIVE_INPUT);
    else if((!strcmp(operation,"readback") || !strcmp(operation,"absent")) && output_path(path))
        n=snprintf(command,sizeof(command),"IFSYS cp %s %s",path,FILM_NATIVE_INPUT);
    else if(!strcmp(operation,"write") && output_path(path))
        n=snprintf(command,sizeof(command),"IFSYS cp %s %s",FILM_NATIVE_OUTPUT,path);
    else if(!strcmp(operation,"flush") && output_path(path)) {
        n=snprintf(command,sizeof(command),"IFSYS flush %s",path);flushing=1;
    }
    if(n<1 || (unsigned)n>=sizeof(command))return report(2,0,"operation_or_path","none",0);
    const char *absence_path=!strcmp(operation,"absent")?path:NULL;
    if(absence_path && !input_absent())return report(1,0,"input_not_absent",operation,0);
    if(!monitor_matches())return report(3,0,"monitor_pin",operation,0);
    signal(SIGHUP,SIG_IGN);signal(SIGPIPE,SIG_IGN);
    signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);
    struct transport t={-1,-1,-1};
    if(transport_start(&t)) {
        transport_stop(&t);return report(5,0,"startup",operation,0);
    }
    int drained=drain_startup(&t);
    if(drained) {
        transport_stop(&t);
        return report(5,0,drained==-2?"startup_busy":drained==-3?"history_capacity":"startup",operation,0);
    }
    int result=exchange(&t,command,operation,flushing,absence_path);
    /* Killing the monitor client does not cancel or prove termination of a
     * Sony in-process operation; uncertain=1 remains authoritative for caller. */
    transport_stop(&t);
    return result;
}

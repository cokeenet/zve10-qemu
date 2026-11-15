#define _GNU_SOURCE
#include "film_runtime_log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int film_log_open(struct film_log *log,const char *path,int rotate) {
    memset(log,0,sizeof(*log));log->fd=-1;log->trusted=1;
    int fd=open(path,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd<0){log->io_error=1;log->trusted=0;return -1;}
    /* A successful atomic exclusive create yields our new empty regular file
     * with permissions no broader than0600. The trusted /tmp_bt parent and
     * absence of hostile same-UID concurrent mutation are explicit premises.
     * Avoid modern stat entrypoints absent from the camera's old libc. */
    if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0) {
        close(fd);log->io_error=1;log->trusted=0;return -1;
    }
    log->fd=fd;log->rotate=!!rotate;return 0;
}
static int record(struct film_log *log,const char *p,unsigned bytes) {
    if(log->fd<0 || log->io_error)return -1;
    if(bytes>FILM_LOG_CAP-log->used) {
        if(!log->rotate){log->io_error=1;return -1;}
        if(ftruncate(log->fd,0)){log->io_error=1;log->trusted=0;return -1;}
        log->used=0;++log->rotations;
    }
    unsigned start=log->used,done=0;
    while(done<bytes) {
        ssize_t n=pwrite(log->fd,p+done,bytes-done,(off_t)start+done);
        if(n>0){done+=(unsigned)n;continue;}
        if(n<0 && errno==EINTR)continue;
        log->io_error=1;
        if(ftruncate(log->fd,(off_t)start))log->trusted=0;
        return -1;
    }
    log->used+=bytes;return 0;
}
static int feed(struct film_log *log,const char *p,size_t bytes) {
    int result=0;
    for(size_t i=0;i<bytes;++i) {
        if(log->dropping) {
            result=-1;
            if(p[i]=='\n') {
                const char *drop=log->dropping==2u?
                    "FILM_LOG_DROP reason=invalid_byte\n":"FILM_LOG_DROP reason=record_size\n";
                (void)record(log,drop,(unsigned)strlen(drop));log->dropping=0;
            }
            continue;
        }
        if(!p[i]) {
            log->pending=0;log->dropping=2;log->bad_record=1;result=-1;continue;
        }
        if(log->pending==sizeof(log->line)) {
            log->pending=0;log->dropping=1;log->bad_record=1;result=-1;
            if(p[i]=='\n') {
                static const char drop[]="FILM_LOG_DROP reason=record_size\n";
                (void)record(log,drop,sizeof(drop)-1u);log->dropping=0;
            }
            continue;
        }
        log->line[log->pending++]=p[i];
        if(p[i]=='\n') {
            if(record(log,log->line,log->pending))result=-1;
            log->pending=0;
        }
    }
    return result;
}
int film_log_printf(struct film_log *log,const char *format,...) {
    char line[FILM_LOG_RECORD+1u];va_list args;va_start(args,format);
    int n=vsnprintf(line,sizeof(line),format,args);va_end(args);
    if(n<0 || (unsigned)n>=sizeof(line)) {
        log->pending=0;log->dropping=1;log->bad_record=1;return -1;
    }
    return feed(log,line,(size_t)n)?-1:n;
}
int film_log_puts(struct film_log *log,const char *line) {
    int a=feed(log,line,strlen(line));
    int b=feed(log,"\n",1);return a||b?-1:0;
}
int film_log_putchar(struct film_log *log,int c) {
    char byte=(char)(unsigned char)c;
    return feed(log,&byte,1)?EOF:(unsigned char)c;
}
int film_log_close(struct film_log *log) {
    if(log->pending || log->dropping) {
        static const char partial[]="FILM_LOG_DROP reason=incomplete\n";
        log->bad_record=1;log->pending=log->dropping=0;
        (void)record(log,partial,sizeof(partial)-1u);
    }
    int result=log->io_error || log->bad_record?-1:0;
    if(log->fd>=0 && close(log->fd))result=-1;
    log->fd=-1;return result;
}

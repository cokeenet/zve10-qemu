/* Bounded ptrace capture barrier for PID149's CPU threads, not for DMA. */
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <sys/syscall.h>
static pid_t freeze_tids[512];
static unsigned freeze_count,freeze_attached;
/* Successful detach is recorded independently: a partial resume must not
 * detach successful entries twice or discard failed entries on a new freeze. */
static unsigned char freeze_owned[512],freeze_stopped[512];
static unsigned char freeze_stop_signals[512];
static unsigned long long freeze_begin;
static unsigned long long freeze_ms(void) {
    struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;
    return (unsigned long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int freeze_enumerate(pid_t *out,unsigned *count) {
    DIR *dir=opendir("/proc/149/task");struct dirent *entry;unsigned n=0;
    if(!dir)return -90;
    while((entry=readdir(dir))) {
        char *end;long id=strtol(entry->d_name,&end,10);
        if(!*entry->d_name || *end || id<=0)continue;
        if(n>=512){closedir(dir);return -91;}out[n++]=(pid_t)id;
    }
    closedir(dir);*count=n;return 0;
}
static int freeze_resume(void) {
    int result=0;unsigned resumed=0;
    for(unsigned i=freeze_attached;i;i--) {
        unsigned n=i-1;int status;
        if(!freeze_owned[n])continue;
        if(!freeze_stopped[n]) {
            pid_t got=waitpid(freeze_tids[n],&status,__WALL|WNOHANG);
            if(got==freeze_tids[n] && WIFSTOPPED(status)) {
                freeze_stopped[n]=1;freeze_stop_signals[n]=(unsigned char)WSTOPSIG(status);
            }
            else if(got==freeze_tids[n] && (WIFEXITED(status)||WIFSIGNALED(status))) {
                freeze_owned[n]=0;continue;
            } else {result=-92;continue;}
        }
        if(ptrace(PTRACE_DETACH,freeze_tids[n],0,0))result=-92;
        else {freeze_owned[n]=freeze_stopped[n]=0;resumed++;}
    }
    printf("FREEZE threads=%u attached=%u resumed=%u result=%d elapsed_ms=%llu\n",
           freeze_count,freeze_attached,resumed,result,freeze_ms()-freeze_begin);
    return result;
}
static int freeze_process(void) {
    pid_t check[512];unsigned confirmed=0;int status,result;
    for(unsigned i=0;i<freeze_attached;i++)if(freeze_owned[i])return -100;
    freeze_count=freeze_attached=0;freeze_begin=freeze_ms();
    if(!freeze_begin)return -93;
    result=freeze_enumerate(freeze_tids,&freeze_count);
    if(result || !freeze_count)return result?result:-94;
    for(unsigned i=0;i<freeze_count;i++) {
        if(ptrace(PTRACE_ATTACH,freeze_tids[i],0,0)){result=-95;goto fail;}
        freeze_owned[i]=1;freeze_stopped[i]=freeze_stop_signals[i]=0;
        freeze_attached++;
        for(;;) {
            pid_t got=waitpid(freeze_tids[i],&status,__WALL|WNOHANG);
            if(got==freeze_tids[i] && WIFSTOPPED(status)) {
                freeze_stopped[i]=1;freeze_stop_signals[i]=(unsigned char)WSTOPSIG(status);break;
            }
            if(got<0 || freeze_ms()-freeze_begin>2000){result=-96;goto fail;}
            usleep(1000);
        }
    }
    result=freeze_enumerate(check,&confirmed);
    if(result || confirmed!=freeze_count){result=-97;goto fail;}
    for(unsigned i=0;i<confirmed;i++) {
        unsigned j;for(j=0;j<freeze_count;j++)if(check[i]==freeze_tids[j])break;
        if(j==freeze_count){result=-98;goto fail;}
    }
    return 0;
fail:
    if(freeze_resume())return -99;
    return result;
}
/* Rebuild rather than trust an old barrier after partial resume. This remains
 * a PID149 CPU barrier, not owner discovery, VFS or USB/DMA quiescence. */
static inline int freeze_recover_process(void) {
    if(freeze_resume())return -99;
    return freeze_process();
}

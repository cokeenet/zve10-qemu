/* External, bounded read-only settings relay. No Sony calls, ptrace, process
 * memory writes, SD operations or worker execution. The INI is diagnostic
 * configuration, not a durable authorization for a future photo worker. */
#define _GNU_SOURCE
#include "film_settings_snapshot.h"
#include "film_settings_io.h"
#include "film_settings_gate.h"
#include "film_settings_maps.h"
#include "film_settings_relay_build.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define CONFIG "/tmp_bt/a6400-film-menu.ini"
#define LOCK "/tmp_bt/a6400-film-menu.lock"
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static int now_ms(uint32_t *out) {
    struct timespec t;
    if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return -1;
    *out=(uint32_t)((uint64_t)t.tv_sec*1000u+(unsigned)t.tv_nsec/1000000u);
    return 0;
}
static int module_matches(void) {
    unsigned char data[4096];uint32_t hash=2166136261u,total=0;ssize_t n;
    int fd=open(FILM_RELAY_MODULE,O_RDONLY);if(fd<0)return 0;
    while((n=read(fd,data,sizeof(data)))>0) {
        if(total>FILM_RELAY_MODULE_BYTES || (unsigned)n>FILM_RELAY_MODULE_BYTES-total){close(fd);return 0;}
        for(ssize_t i=0;i<n;i++)hash=(hash^data[i])*16777619u;
        total+=(unsigned)n;
    }
    close(fd);
    return n==0 && total==FILM_RELAY_MODULE_BYTES && hash==FILM_RELAY_MODULE_FNV;
}
static int sample(struct film_settings *out) {
    char line[512];FILE *f=fopen("/proc/149/comm","r");
    if(!f)return -10;
    int ok=fgets(line,sizeof(line),f)&&!strcmp(line,"im.elf\n");fclose(f);
    if(!ok)return -11;
    struct film_settings_map map={0};f=fopen("/proc/149/maps","r");
    if(!f)return -12;
    while(fgets(line,sizeof(line),f))
        film_settings_map_line(&map,line,FILM_RELAY_MODULE,FILM_RELAY_UI_RVA);
    fclose(f);
    if(!map.image_count)return 10;
    if(map.image_count!=1 || map.state_count!=1)return -13;
    unsigned char first[FILM_UI_SNAPSHOT_BYTES],second[FILM_UI_SNAPSHOT_BYTES];
    int fd=open("/proc/149/mem",O_RDONLY);if(fd<0)return -14;
    ok=pread(fd,first,sizeof(first),map.state)==sizeof(first) &&
       pread(fd,second,sizeof(second),map.state)==sizeof(second);
    close(fd);if(!ok)return -15;
    uint32_t now;if(now_ms(&now))return -16;
    return film_settings_snapshot_get(out,first,second,sizeof(first),now);
}
int main(int argc,char **argv) {
    uint32_t duration=900000u,start,now;char *end;
    if(argc==2) {
        unsigned long seconds=strtoul(argv[1],&end,10);
        if(!argv[1][0]||*end||seconds<1||seconds>900)return 2;
        duration=(uint32_t)seconds*1000u;
    }else if(argc!=1)return 2;
    if(!module_matches() || now_ms(&start))return 3;
    int lock=open(LOCK,O_WRONLY|O_CREAT|O_EXCL,0600);if(lock<0)return 4;
    close(lock);
    signal(SIGHUP,SIG_IGN);signal(SIGINT,stop);signal(SIGTERM,stop);
    struct film_settings last={0,0,2,1};
    if(film_settings_publish(CONFIG,&last)) {
        unlink(CONFIG);unlink(LOCK);return 5;
    }
    printf("RELAY_READY duration_ms=%u module=%s ui_rva=%x PHOTO_WORKER=DISABLED\n",
           duration,FILM_RELAY_MODULE,FILM_RELAY_UI_RVA);fflush(stdout);
    int prior=999,result=0;
    while(!stopping) {
        if(now_ms(&now)){result=6;break;}
        if((uint32_t)(now-start)>=duration)break;
        struct film_settings current=last;
        int status=sample(&current);
        if(status)current.enabled=0;
        int changed=memcmp(&current,&last,sizeof(current))!=0;
        if(changed) {
            if(film_settings_publish(CONFIG,&current)) {
                unlink(CONFIG);result=7;break;
            }
            last=current;
        }
        if(changed||status!=prior) {
            struct film_job job;int gate=film_settings_job(&job,&current);
            printf("RELAY_STATE ms=%u source=%d enabled=%d stock=%d grain=%d halation=%d plan=%d preset=%s PHOTO_WORKER=DISABLED\n",
                   now,status,current.enabled,current.stock,current.grain,current.halation,
                   gate,job.preset?job.preset:"none");fflush(stdout);
            prior=status;
        }
        usleep(250000);
    }
    last.enabled=0;
    if(film_settings_publish(CONFIG,&last)){unlink(CONFIG);result=8;}
    unlink(LOCK);
    printf("RELAY_STOP enabled=0 signal=%d result=%d\n",(int)stopping,result);fflush(stdout);
    return result;
}

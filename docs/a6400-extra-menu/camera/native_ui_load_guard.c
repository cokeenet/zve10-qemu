/* Adapted from sony-a6400-research native_menu_touch_trace_guard.c.
 * Only the owned request pathname differs; preserve exact-version guards. */
/* Exact-version, reversible RAM-only trace hook. No selection/activation. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include "native_freeze.h"
#define CODE 0x700d708u
#define STATE 0x7143920u
#define HOOK 0x6309d48u
#define STATE_SIZE 1184u
static const unsigned char original[12]={0x2d,0xe9,0xf0,0x47,0x88,0xb0,0x4f,0xf0,0x14,0x08,0x01,0x22};
static unsigned char payload[2296],zeros[2296],buffer[2296];
static int readmem(int fd,uint32_t a,void *b,unsigned n){return pread(fd,b,n,a)==(int)n?0:-1;}
static int poke(uint32_t a,const void *b,unsigned n){
    if((a|n)&3)return -1;
    for(unsigned i=0;i<n;i+=4){uint32_t word;memcpy(&word,(const unsigned char *)b+i,4);if(ptrace(PTRACE_POKETEXT,149,(void *)(uintptr_t)(a+i),(void *)(uintptr_t)word))return -1;}
    return 0;
}
static int maps_ok(void){
    FILE *f=fopen("/proc/149/maps","r");if(!f)return 0;
    char line[256],perms[5];unsigned lo,hi;int code=0,state=0;
    while(fgets(line,sizeof(line),f))if(sscanf(line,"%x-%x %4s",&lo,&hi,perms)==3){
        if(lo<=CODE && CODE+2296<=hi && !strcmp(perms,"r-xp") && strstr(line,"/libObj.so"))code=1;
        if(lo<=STATE && STATE+1760<=hi && !strcmp(perms,"rw-p"))state=1;
    }
    fclose(f);return code&&state;
}
int main(void){
    uint32_t h[8];FILE *f=fopen("/tmp_bt/a6400-ui-load.req","rb");if(!f)return 2;
    if(fread(h,1,sizeof(h),f)!=sizeof(h) || h[0]!=0x4d544852 || h[1]!=1 || h[2]>2 || !h[3] || h[3]>sizeof(payload) || h[3]%4 || !(h[4]&1) || (h[4]&~1u)<CODE || (h[4]&~1u)>=CODE+h[3] || h[6]!=STATE_SIZE || h[7]!=HOOK || fread(payload,1,h[3],f)!=h[3] || fgetc(f)!=EOF){fclose(f);return 3;}
    fclose(f);uint32_t sum=2166136261u;for(unsigned i=0;i<h[3];i++)sum=(sum^payload[i])*16777619u;if(sum!=h[5])return 4;
    char comm[32]={0};f=fopen("/proc/149/comm","r");if(!f)return 5;
    int identity=fgets(comm,sizeof(comm),f) && !strcmp(comm,"im.elf\n");fclose(f);if(!identity)return 5;
    unsigned char branch[12]={0xdf,0xf8,0,0xf0,0,0,0,0,0,0xbf,0,0xbf};memcpy(branch+4,&h[4],4);
    unsigned long long t0=freeze_ms(),t1=0,t2=0,t3=0,t4=0;
    int result=freeze_process(),fd=-1,changed=0;t1=freeze_ms();if(result)goto end;
    fd=open("/proc/149/mem",h[2]?O_RDWR:O_RDONLY);if(fd<0){result=-10;goto done;}
    if(!maps_ok()){result=-11;goto done;}
    if(readmem(fd,HOOK,buffer,12) || memcmp(buffer,h[2]==2?branch:original,12)){result=-12;goto done;}
    for(unsigned i=0;i<freeze_count;i++){
        uint32_t regs[18];if(ptrace(PTRACE_GETREGS,freeze_tids[i],0,regs)){result=-13;goto done;}
        uint32_t pc=regs[15]&~1u;
        if((HOOK<=pc && pc<HOOK+12) || (CODE<=pc && pc<CODE+h[3])){result=-14;goto done;}
    }
    t2=freeze_ms();
    if(h[2]==2){
        uint32_t state[8];if(readmem(fd,STATE,state,sizeof(state)) || state[0]!=0x4d544c47 || state[1]!=1 || state[5]!=24 || state[7]!=0x956f0){result=-15;goto done;}
        uint32_t disabled=0;if(pwrite(fd,&disabled,4,STATE+8)!=4){result=-16;goto done;}
        if(state[4]){result=-17;goto done;} /* Inflight call must finish first. */
        if(readmem(fd,CODE,buffer,h[3]) || memcmp(buffer,payload,h[3])){result=-18;goto done;}
        if(poke(HOOK,original,12) || poke(CODE,zeros,h[3]) || pwrite(fd,zeros,STATE_SIZE,STATE)!=STATE_SIZE){result=-19;goto done;}
        t3=freeze_ms();
        if(readmem(fd,HOOK,buffer,12) || memcmp(buffer,original,12) || readmem(fd,CODE,buffer,h[3]) || memcmp(buffer,zeros,h[3]) || readmem(fd,STATE,buffer,STATE_SIZE) || memcmp(buffer,zeros,STATE_SIZE)){result=-20;goto done;}
        printf("REMOVED hook=06309d48 code_restored=1 state_restored=1\n");
    }else{
        if(readmem(fd,CODE,buffer,2296) || memcmp(buffer,zeros,2296) || readmem(fd,STATE,buffer,1760) || memcmp(buffer,zeros,1760)){result=-21;goto done;}
        printf("CHECK op=%u entry=%08x code_bytes=%u owned_zero_padding=1 pc_clear=1\n",h[2],h[4],h[3]);
        if(h[2]==1){
            uint32_t state[8]={0x4d544c47,1,1,0,0,24,0,0x956f0};
            changed=1;
            if(pwrite(fd,state,sizeof(state),STATE)!=sizeof(state) || poke(CODE,payload,h[3]) || poke(HOOK,branch,12)){result=-22;goto rollback;}
            t3=freeze_ms();
            if(readmem(fd,HOOK,buffer,12) || memcmp(buffer,branch,12) || readmem(fd,CODE,buffer,h[3]) || memcmp(buffer,payload,h[3]) || readmem(fd,STATE,buffer,sizeof(state)) || memcmp(buffer,state,sizeof(state))){result=-23;goto rollback;}
            printf("INSTALLED selection=0 capacity=24 code_readback=1 hook_readback=1 state_readback=1\n");
        }
    }
    goto done;
rollback:
    if(changed){int ok=!poke(HOOK,original,12) && !poke(CODE,zeros,h[3]) && pwrite(fd,zeros,STATE_SIZE,STATE)==STATE_SIZE;printf("ROLLBACK result=%d\n",ok?0:-1);if(!ok)result=-99;}
done:
    t4=freeze_ms();
    if(fd>=0)close(fd);
    if(freeze_resume())result=-99;
end:
    printf("PHASE freeze_ms=%llu preflight_ms=%llu write_ms=%llu verify_ms=%llu resume_ms=%llu total_ms=%llu\n",
        t1-t0,t2?t2-t1:0,t3?t3-t2:0,t4&&t3?t4-t3:0,t4?freeze_ms()-t4:0,freeze_ms()-t0);
    printf("END result=%d op=%u errno=%d\n",result,h[2],errno);fflush(stdout);return result?6:0;
}

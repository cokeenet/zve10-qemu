#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef void *(*get_instance_fn)(void);
typedef void (*get_position_fn)(const void *,unsigned char,short *,short *);
typedef int (*get_button_fn)(const void *,unsigned char,unsigned char);

int a6400_ui_attach_v1(void *system,unsigned tid)
{
    FILE *log=fopen("/tmp_bt/a6400-native-ui-attach.log","a");
    if(!log)return -1;
    fprintf(log,"UI_ATTACH_BEGIN tid=%u actual=%ld system=%p\n",tid,syscall(SYS_gettid),system);
    if(tid!=(unsigned)syscall(SYS_gettid) || system!=(void *)0x956f0) {
        fprintf(log,"UI_ATTACH_INVALID_CONTEXT\n");fclose(log);return -2;
    }
    get_instance_fn get_system=(get_instance_fn)dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    get_position_fn position=(get_position_fn)dlsym(RTLD_DEFAULT,
        "_ZNK2ux6wgtsys12WidgetSystem24getMousePosInFramebufferEhRsS2_");
    get_button_fn button=(get_button_fn)dlsym(RTLD_DEFAULT,
        "_ZNK2ux6wgtsys12WidgetSystem13getMouseStateEhh");
    fprintf(log,"UI_API instance=%p position=%p button=%p\n",
            (void *)get_system,(void *)position,(void *)button);
    if((unsigned long)get_system!=0x06308d31 ||
       (unsigned long)position!=0x06309019 || (unsigned long)button!=0x06309045) {
        fprintf(log,"UI_ATTACH_API_MISMATCH\n");fclose(log);return -3;
    }
    void *actual=get_system();
    if(actual!=system) {fprintf(log,"UI_ATTACH_SINGLETON_MISMATCH actual=%p\n",actual);fclose(log);return -4;}
    short x=0,y=0;
    position(system,0,&x,&y);
    int down=button(system,0,0);
    const char *names[]={"_ZN9GEN_PanelC1Ev","_ZN9GEN_LabelC1Ev",
        "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE",
        "_ZN2ux6wgtsys10WidgetBase10setAnimateEb"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)
        fprintf(log,"UI_AVAILABLE %s=%p\n",names[i],dlsym(RTLD_DEFAULT,names[i]));
    char path[80],name[80]={0};
    snprintf(path,sizeof(path),"/proc/self/task/%u/comm",tid);
    FILE *comm=fopen(path,"r");
    if(comm){if(!fgets(name,sizeof(name),comm))name[0]=0;fclose(comm);}
    fprintf(log,"UI_NATIVE_THREAD_OK xy=%d,%d down=%d thread=%s",x,y,down,name);
    if(!name[0])fputc('\n',log);
    fclose(log);
    return 0;
}

/* Exact-version one-shot loader, called AFTER a natural native release.
 * State and trampoline match the reviewed research release-trace contract.
 * No remote arbitrary-thread call and no persistent application-code patch.
 */
typedef unsigned int u32;
typedef unsigned char u8;
#define STATE 0x07143920u
#define SYSTEM 0x000956f0u
#ifndef A6400_UI_MODULE_PATH
#define A6400_UI_MODULE_PATH "/tmp_bt/a6400-ui-native.so"
#endif
struct event {
    u32 sequence,tid,system,before_xy,mouse_button,after_xy,raw_xy,buttons;
    u32 module_handle,attach_result,native_result,transport_tag;
};
struct trace {
    u32 magic,version,enabled,count,inflight,capacity,error,expected_system;
    struct event events[24];
};
extern u32 mt_original_release(void *,void *) __attribute__((visibility("hidden")));
typedef void *(*open_fn)(const char *,int);
typedef void *(*symbol_fn)(void *,const char *);
typedef int (*attach_fn)(void *,u32);

static u32 current_tid(void)
{
    register u32 r0 __asm__("r0")=0;
    register u32 r7 __asm__("r7")=224;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory","cc");
    return r0;
}

u32 ui_release_loader(void *system,void *message)
{
    volatile struct trace *s=(volatile struct trace *)STATE;
    if(s->magic!=0x4d544c47 || s->version!=1)
        return mt_original_release(system,message);
    __sync_fetch_and_add(&s->inflight,1);
    u32 mouse=4,before=0,tag=0;
    if((u32)system==SYSTEM && message) {
        mouse=((const u8 *)message)[20];
        tag=*(const u32 *)((const u8 *)message+16);
        if(mouse<4) before=*(const u32 *)((const u8 *)system+mouse*20+0x4a8);
    }
    u32 result=mt_original_release(system,message);
    if(mouse<4 && __sync_bool_compare_and_swap(&s->enabled,1,0)) {
        volatile struct event *e=&s->events[0];
        e->tid=current_tid();e->system=(u32)system;e->before_xy=before;
        e->mouse_button=mouse;e->transport_tag=tag;e->native_result=result;
        const u8 *p=(const u8 *)system+mouse*20;
        e->after_xy=*(const u32 *)(p+0x4a8);
        e->raw_xy=*(const u32 *)(p+0x4ac);e->buttons=*(const u32 *)(p+0x4b0);
        open_fn load=(open_fn)0x04028bb1u;
        symbol_fn symbol=(symbol_fn)0x04028c49u;
        void *module=load(A6400_UI_MODULE_PATH,2);
        e->module_handle=(u32)module;
        if(!module) s->error=2;
        else {
            attach_fn attach=(attach_fn)symbol(module,"a6400_ui_attach_v1");
            if(!attach) s->error=3;
            else {
                int attached=attach(system,e->tid);
                e->attach_result=(u32)attached;
                if(attached) s->error=4;
            }
        }
        __sync_synchronize();e->sequence=1;s->count=1;
    }
    __sync_fetch_and_sub(&s->inflight,1);
    return result;
}

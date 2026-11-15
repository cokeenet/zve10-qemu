#ifndef A6400_UI_TRIAL_TRACE_H
#define A6400_UI_TRIAL_TRACE_H
#include <stdint.h>
/* Fits exactly in unused loader events 1..23. UI producer performs only
 * bounded stores and barriers; background shell does all persistent I/O. */
struct ui_trial_trace { uint32_t header[12], records[22][12]; };
_Static_assert(sizeof(struct ui_trial_trace)==1104,"owned trace size");
static inline uint32_t ui_trial_hash(const char *name) {
    uint32_t value=2166136261u;
    for(unsigned i=0;i<64 && name[i];i++)value=(value^(unsigned char)name[i])*16777619u;
    return value;
}
static inline void ui_trial_record(volatile struct ui_trial_trace *trace,
    const char *name,uint32_t ms,uint32_t tid,uint32_t a,uint32_t b,
    uint32_t ticks,uint32_t flags) {
    uint32_t seq=trace->header[2]+1u;
    volatile uint32_t *r=trace->records[(seq-1u)%22u];
    r[0]=seq*2u+1u;
    __sync_synchronize();
    r[1]=ui_trial_hash(name);r[2]=ms;r[3]=tid;r[4]=a;r[5]=b;
    r[6]=ticks;r[7]=flags;r[8]=r[9]=r[10]=0;
    __sync_synchronize();r[11]=seq*2u;
    __sync_synchronize();r[0]=seq*2u;
    trace->header[0]=0x33544955u;trace->header[1]=1;
    trace->header[4]=ms;trace->header[5]=149;trace->header[6]=tid;
    trace->header[7]=22;trace->header[8]=48;
    __sync_synchronize();trace->header[2]=seq;
}
#endif

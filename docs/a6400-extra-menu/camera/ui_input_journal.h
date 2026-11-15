#ifndef A6400_UI_INPUT_JOURNAL_H
#define A6400_UI_INPUT_JOURNAL_H
#include <stdint.h>
#define UI_INPUT_STAGES 9u
#define UI_INPUT_OWNED 18u
/* Raw words are deliberately not treated as validated native class handles. */
struct ui_input_owned { uint32_t address,flags_mode,tree_word10,key_parent; };
struct ui_input_snapshot {
    uint32_t begin,stage,ms,tid,key_target,hover[4],capture[4],transient[2];
    uint32_t animate_count,widget_count,misc[2],capture_mask,owned_count;
    struct ui_input_owned owned[UI_INPUT_OWNED];
    uint32_t end;
};
struct ui_input_journal {
    uint32_t magic,version,snapshot_size,sequence;
    struct ui_input_snapshot snapshots[UI_INPUT_STAGES];
};
static inline uint32_t ui_input_word(const void *object,unsigned offset) {
    return *(const volatile uint32_t *)((const unsigned char *)object+offset);
}
/* Known-version, UI-thread-only producer. No native setters, system calls,
 * dynamic allocation, disk I/O, external-widget dereference or input writes. */
static inline int ui_input_capture(volatile struct ui_input_journal *journal,
    unsigned stage,const void *system,void *const *owned,unsigned count,
    uint32_t ms,uint32_t tid) {
    if(!journal || !system || stage>=UI_INPUT_STAGES || count>UI_INPUT_OWNED ||
       (count && !owned))return -1;
    uint32_t sequence=journal->sequence+1u;
    volatile struct ui_input_snapshot *s=&journal->snapshots[stage];
    s->begin=0;__sync_synchronize();
    s->stage=stage;s->ms=ms;s->tid=tid;
    s->key_target=ui_input_word(system,0x4f8);
    for(unsigned i=0;i<4;i++) {
        s->hover[i]=ui_input_word(system,0x4b4+0x14*i);
        s->capture[i]=ui_input_word(system,0x4b8+0x14*i);
    }
    s->transient[0]=ui_input_word(system,0x4fc);
    s->transient[1]=ui_input_word(system,0x500);
    s->animate_count=ui_input_word(system,0x510);
    s->widget_count=ui_input_word(system,0x514);
    s->misc[0]=ui_input_word(system,0x518);
    s->misc[1]=ui_input_word(system,0x51c);
    s->capture_mask=ui_input_word(system,0x520);s->owned_count=count;
    for(unsigned i=0;i<UI_INPUT_OWNED;i++) {
        void *p=i<count?owned[i]:0;
        s->owned[i].address=(uint32_t)(uintptr_t)p;
        s->owned[i].flags_mode=p?ui_input_word(p,0x20):0;
        s->owned[i].tree_word10=p?ui_input_word(p,0x10):0;
        s->owned[i].key_parent=p?ui_input_word(p,0x2c):0;
    }
    __sync_synchronize();s->end=sequence;
    __sync_synchronize();s->begin=sequence;
    journal->magic=0x31494e55u;journal->version=1;
    journal->snapshot_size=sizeof(*s);
    __sync_synchronize();journal->sequence=sequence;
    return 0;
}
#endif

#include "film_settings_snapshot.h"
#include <stdint.h>
#include <string.h>

/* Match an explicitly persistent UI build; finite readers retain their
 * historical ten-minute deadline. Running/heartbeat checks always apply. */
#ifndef FILM_SETTINGS_PERSISTENT
#define FILM_SETTINGS_PERSISTENT 0
#endif
#if FILM_SETTINGS_PERSISTENT != 0 && FILM_SETTINGS_PERSISTENT != 1
#error "FILM_SETTINGS_PERSISTENT must be 0 or 1"
#endif

static uint32_t word(const void *p,unsigned at) {
    uint32_t n;memcpy(&n,(const unsigned char *)p+at,4);return n;
}
int film_settings_snapshot_get(struct film_settings *out,const void *first,
                               const void *second,unsigned length,unsigned now) {
    if(!out || !first || !second || length!=FILM_UI_SNAPSHOT_BYTES)return -1;
    static const unsigned fixed[]={0x68,0xc0,0xc4,0x18c,0x190};
    if(memcmp(first,second,16))return 2;
    for(unsigned i=0;i<sizeof(fixed)/sizeof(*fixed);i++)
        if(word(first,fixed[i])!=word(second,fixed[i]))return 2;
    if(word(second,0x68)!=0x956f0 || word(second,0xc0)!=380 ||
       word(second,0x18c)!=1)return -1;
    static const unsigned maximum[4]={1,4,3,1};
    for(unsigned i=0;i<4;i++)if(word(second,i*4)>maximum[i])return -1;
    if(word(second,0x190)!=1 || !word(second,0xc4) ||
#if !FILM_SETTINGS_PERSISTENT
       (uint32_t)(now-word(second,0xc4))>=600000u ||
#endif
       (uint32_t)(now-word(second,0xc8))>2000u)return 1;
    struct film_settings result={(int)word(second,0),(int)word(second,4),
        (int)word(second,8),(int)word(second,12)};
    *out=result;return 0;
}

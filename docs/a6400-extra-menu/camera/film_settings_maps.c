#include "film_settings_maps.h"
#include "film_settings_snapshot.h"
#include <stdio.h>
#include <string.h>
void film_settings_map_line(struct film_settings_map *m,const char *line,
                            const char *module,uint32_t rva) {
    unsigned low,high,offset;char permissions[5];int at=0;
    if(!m||!line||!module||sscanf(line,"%x-%x %4s %x %*s %*s %n",
        &low,&high,permissions,&offset,&at)!=4 || at<=0 || low>=high)return;
    size_t n=strcspn(line+at,"\r\n");
    if(strlen(module)!=n || memcmp(line+at,module,n))return;
    if(!offset && !strcmp(permissions,"r-xp")) {
        ++m->image_count;m->base=low;
        m->state=(rva<=UINT32_MAX-low)?low+rva:0;
    }
    if(m->image_count==1 && m->state>=low &&
       m->state<=UINT32_MAX-FILM_UI_SNAPSHOT_BYTES &&
       m->state+FILM_UI_SNAPSHOT_BYTES<=high &&
       !strcmp(permissions,"rw-p"))++m->state_count;
}

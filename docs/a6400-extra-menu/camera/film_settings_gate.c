#include "film_settings_gate.h"
int film_settings_job(struct film_job *out,const struct film_settings *s) {
    char validated[128];
    if(!out)return -1;
    *out=(struct film_job){0};
    if(film_settings_format(s,validated,sizeof(validated))<1)return -1;
    if(!s->enabled)return 0;
    out->preset=film_stock_worker_name(s->stock);
    out->grain[0]=(char)('0'+s->grain);
    out->halation[0]=(char)('0'+s->halation);
    return 1;
}

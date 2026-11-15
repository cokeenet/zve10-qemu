#include "film_native_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned now, drawings, visibility_calls, persistence;
static int visible;
static char labels[6][80];
static struct film_settings saved;
static void show(void *p, int active) {
    (void)p; visible=active; ++visibility_calls;
}
static void draw(void *p,const struct film_menu *m) {
    (void)p; ++drawings;
    for(unsigned i=0;i<6;i++) {
        char text[80];int x,y,w,h;
        int used=film_native_view_row(m,i,text,sizeof(text),&x,&y,&w,&h);
        assert(used>=0);
        if(used)assert(x==28 && w==584 && y>=88 && y+h<=480);
        strcpy(labels[i],used?text:"");
    }
}
static void persist(void *p,const struct film_settings *s) {
    (void)p; saved=*s; ++persistence;
}
static void tick(struct film_native_view *v,int menu,int zoom,int other,int touch,int x,int y) {
    now+=20; film_native_view_step(v,now,menu,zoom,other,touch,x,y);
}
static void chord(struct film_native_view *v) {
    tick(v,0,0,0,6,0,0);
    for(int i=0;i<34;i++) tick(v,1,1,0,6,0,0);
    tick(v,0,0,0,6,0,0);
}
static void tap(struct film_native_view *v,int x,int y) {
    tick(v,0,0,0,5,x,y);
    tick(v,0,0,0,6,32767,32767);
}
int main(void) {
    const struct film_native_view_ops ops={show,draw,persist};
    struct film_native_view v;
    film_native_view_init(&v,&ops,0,0);
    assert(!v.menu.visible && !drawings && !persistence);
    chord(&v);assert(visible && v.menu.visible && visibility_calls==1);
    assert(!strcmp(labels[0],"LUT: OFF"));assert(!strcmp(labels[1],"Film: Portra 400"));
    unsigned previous=drawings;
    for(int i=0;i<20;i++)tick(&v,0,0,0,6,32767,32767);
    assert(drawings==previous); /* Release enum 6 is not truthy/down. */
    tick(&v,0,0,0,5,80,140);assert(!persistence && v.menu.selected==0);
    previous=drawings;
    for(int i=0;i<10;i++)tick(&v,0,0,0,5,80,140);
    assert(drawings==previous); /* No repeated rendering for a held touch. */
    tick(&v,0,0,0,6,32767,32767);
    assert(persistence==1 && saved.enabled==1 && !strcmp(labels[0],"LUT: ON"));
    tick(&v,0,0,0,6,32767,32767);assert(persistence==1);
    tap(&v,80,220);assert(v.menu.page==FILM_MENU_STOCKS);
    assert(!strcmp(labels[2],"Fuji Provia"));
    tap(&v,80,210);assert(v.menu.page==FILM_MENU_HOME && v.menu.settings.stock==2);
    assert(persistence==2 && saved.stock==2);
    tick(&v,0,0,1,6,0,0);assert(!visible && !v.menu.visible);
    chord(&v);assert(visible && v.menu.settings.enabled && v.menu.settings.stock==2);
    tap(&v,80,300);assert(!visible && !v.menu.visible);
    chord(&v);assert(visible);
    tick(&v,0,0,0,5,80,140); /* Incomplete gesture followed by invalid input. */
    tick(&v,0,0,0,0,80,140);assert(!visible && persistence==2);
    film_native_view_hide(&v);assert(!visible && persistence==2);
    assert(film_native_view_row(&v.menu,6,labels[0],80,0,0,0,0)==-1);
    puts("NATIVE_FILM_VIEW_OK");
}

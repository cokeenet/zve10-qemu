#ifndef A6400_FILM_NATIVE_VIEW_H
#define A6400_FILM_NATIVE_VIEW_H
#include "film_menu.h"

struct film_native_view_ops {
    void (*visibility)(void *,int);
    void (*draw)(void *,const struct film_menu *);
    void (*settings)(void *,const struct film_settings *);
};
struct film_native_view {
    struct film_menu menu;
    struct film_native_view_ops ops;
    void *context;
    int drawn, drawn_page, drawn_selected, drawn_enabled, drawn_stock;
};
void film_native_view_init(struct film_native_view *,const struct film_native_view_ops *,
                           void *,const struct film_settings *);
unsigned film_native_view_step(struct film_native_view *,unsigned,
                               int,int,int,int,int,int);
void film_native_view_hide(struct film_native_view *);
int film_native_view_row(const struct film_menu *,unsigned,char *,unsigned,
                         int *,int *,int *,int *);
#endif

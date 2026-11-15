#ifndef A6400_FILM_SETTINGS_GATE_H
#define A6400_FILM_SETTINGS_GATE_H
#include "film_menu.h"
struct film_job { const char *preset; char grain[2],halation[2]; };
/* Selection only: does not execute a worker or authorize use of a stale file.
 * 1=valid ON plan;0=OFF;-1=invalid. Always clears a previous output plan. */
int film_settings_job(struct film_job *,const struct film_settings *);
#endif

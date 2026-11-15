#ifndef A6400_FILM_SETTINGS_IO_H
#define A6400_FILM_SETTINGS_IO_H
#include "film_menu.h"
/* Background-process-only atomic config publication. Never call from Sony UI. */
int film_settings_publish(const char *path,const struct film_settings *);
#endif

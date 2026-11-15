#ifndef A6400_FILM_SETTINGS_SNAPSHOT_H
#define A6400_FILM_SETTINGS_SNAPSHOT_H
#include "film_menu.h"
#define FILM_UI_SNAPSHOT_BYTES 1120u
/* Exact diagnostic UI layout, not a general Sony ABI. Input buffers must each
 * contain length bytes. The caller validates the exact module identity first.
 * 0=current;1=inactive/stale;2=changing;-1=invalid. Output changes only on0. */
int film_settings_snapshot_get(struct film_settings *,const void *,const void *,
                               unsigned length,unsigned now_ms);
#endif

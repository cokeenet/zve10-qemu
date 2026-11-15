#ifndef A6400_FILM_SETTINGS_SOURCE_H
#define A6400_FILM_SETTINGS_SOURCE_H
#include "film_settings_snapshot.h"
#include <stdint.h>
struct film_settings_source {
    const char *proc, *module;
    uint32_t bytes, fnv, rva;
};
int film_settings_now(uint32_t *);
int film_file_matches(const char *, uint32_t bytes, uint32_t fnv);
/* Read only. Validates artifact, process, unique mapping and fresh live state.
 * Output is unchanged unless return0. An INI is never consulted. */
int film_settings_source_get(struct film_settings *, const struct film_settings_source *);
#endif

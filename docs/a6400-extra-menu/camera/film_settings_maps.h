#ifndef A6400_FILM_SETTINGS_MAPS_H
#define A6400_FILM_SETTINGS_MAPS_H
#include <stdint.h>
struct film_settings_map { uint32_t base,state,image_count,state_count; };
void film_settings_map_line(struct film_settings_map *,const char *line,
                            const char *exact_module,uint32_t ui_rva);
#endif

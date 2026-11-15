#ifndef A6400_FILM_MENU_H
#define A6400_FILM_MENU_H

/* Portable control/state only. The camera adapter must supply a verified native
 * display transaction and input handoff; this module never writes camera RAM. */
struct film_settings { int enabled, stock, grain, halation; };
enum film_menu_page { FILM_MENU_HOME, FILM_MENU_STOCKS };
enum film_menu_change {
    FILM_MENU_REDRAW = 1, FILM_MENU_OPENED = 2, FILM_MENU_CLOSED = 4,
    FILM_MENU_SETTINGS_CHANGED = 8
};
struct film_menu {
    struct film_settings settings;
    int visible, page, selected;
    unsigned last_sample, hold_started;
    int have_sample, released, pending, fired;
    int touch_ready, touching, touch_row, touch_cancelled;
};

void film_menu_init(struct film_menu *, const struct film_settings *);
unsigned film_menu_input(struct film_menu *, unsigned now_ms,
                         int menu_down, int magnifier_down, int other_native_activity);
unsigned film_menu_touch(struct film_menu *, unsigned now_ms, int down, int x, int y);
unsigned film_menu_close(struct film_menu *);
int film_menu_set_stock(struct film_menu *, int stock);
int film_menu_hit(int page, int x, int y);
const char *film_stock_worker_name(int stock);
const char *film_stock_display_name(int stock);
int film_settings_format(const struct film_settings *, char *out, unsigned capacity);
int film_settings_parse(struct film_settings *, const char *text, unsigned length);
#endif

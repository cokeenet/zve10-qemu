/* Behavior tests for the controller; no camera, framebuffer or native-input writes. */
#include "film_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned now;
static unsigned sample(struct film_menu *m, int menu, int zoom, int other) {
    now += 100;
    return film_menu_input(m, now, menu, zoom, other);
}
static void chord(struct film_menu *m) {
    sample(m, 0, 0, 0);
    for (int i = 0; i < 7; ++i) sample(m, 1, 1, 0);
}
static unsigned tap(struct film_menu *m, int row) {
    int y = m->page == FILM_MENU_HOME ? 116 + row * 80 + 20 : 88 + row * 56 + 15;
    film_menu_touch(m, now += 20, 0, 32767, 32767);
    film_menu_touch(m, now += 20, 1, 100, y);
    return film_menu_touch(m, now += 20, 0, 32767, 32767);
}
int main(void) {
    struct film_menu m;
    struct film_settings settings = {0, 0, 2, 1}, before;
    char text[128];
    film_menu_init(&m, &settings);
    /* Held keys at startup, stale observations and single keys never summon. */
    for (int i = 0; i < 12; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    sample(&m, 0, 0, 0);
    for (int i = 0; i < 12; ++i) sample(&m, 1, 0, 0);
    assert(!m.visible);
    sample(&m, 0, 0, 0); sample(&m, 1, 1, 0);
    now += 1000; sample(&m, 1, 1, 0);
    for (int i = 0; i < 5; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    assert(sample(&m, 1, 1, 0) & FILM_MENU_OPENED);
    /* A fired continuous hold must survive a dispatch gap without toggling. */
    now += 1000; sample(&m, 1, 1, 0);
    for (int i = 0; i < 10; ++i) sample(&m, 1, 1, 0);
    assert(m.visible);
    chord(&m); assert(!m.visible);
    /* Invalid cache samples still require a new full release. */
    sample(&m, 0x7fffffff, 1, 0);
    for (int i = 0; i < 10; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    chord(&m);
    assert(m.visible && m.page == FILM_MENU_HOME);
    for (int i = 0; i < 20; ++i) sample(&m, 1, 1, 0);
    assert(m.visible); /* One continuous hold toggles once. */
    assert(tap(&m, 0) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.enabled == 1);
    assert(tap(&m, 1) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_STOCKS);
    assert(tap(&m, 3) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.stock == 3 && m.page == FILM_MENU_HOME);
    assert(tap(&m, 2) & FILM_MENU_CLOSED);
    assert(!m.visible && m.settings.enabled && m.settings.stock == 3);
    for (int i = 0; i < 3; ++i) {
        chord(&m); assert(m.visible);
        assert(sample(&m, 0, 0, 1) & FILM_MENU_CLOSED);
        assert(!m.visible && m.settings.stock == 3 && m.settings.enabled);
    }
    before = m.settings;
    assert(sample(&m, 0, 0, 1) == 0); /* Hidden native input passes untouched. */
    assert(film_menu_touch(&m, now, 1, 100, 140) == 0);
    assert(memcmp(&before, &m.settings, sizeof before) == 0);
    chord(&m);
    /* Opening with a finger down does not accidentally enable/disable. */
    film_menu_touch(&m, now += 20, 1, 100, 140);
    film_menu_touch(&m, now += 20, 0, 32767, 32767);
    assert(m.settings.enabled);
    /* Dragging outside the original hit target cancels the activation. */
    film_menu_touch(&m, now += 20, 1, 100, 140);
    film_menu_touch(&m, now += 20, 1, 100, 300);
    film_menu_touch(&m, now += 20, 0, 32767, 32767);
    assert(m.settings.enabled);
    for (int stock = 0; stock < 5; ++stock) {
        tap(&m, 1); assert(m.page == FILM_MENU_STOCKS);
        tap(&m, stock); assert(m.settings.stock == stock);
    }
    assert(strcmp(film_stock_worker_name(m.settings.stock), "tri_x_400") == 0);
    assert(film_stock_worker_name(-1) == NULL && film_stock_worker_name(5) == NULL);
    chord(&m); assert(!m.visible); chord(&m); assert(m.visible);
    /* All real persisted choices round-trip, independent of menu visibility. */
    for (int enabled = 0; enabled < 2; ++enabled)
        for (int stock = 0; stock < 5; ++stock)
            for (int grain = 0; grain < 4; ++grain)
                for (int halo = 0; halo < 2; ++halo) {
                    struct film_settings a = {enabled, stock, grain, halo}, b = {0};
                    int n = film_settings_format(&a, text, sizeof text);
                    assert(n > 0 && film_settings_parse(&b, text, (unsigned)n) == 0);
                    assert(memcmp(&a, &b, sizeof a) == 0);
                    assert(film_settings_format(&a, text, 8) < 0);
                }
    const char *bad[] = {
        "ENABLED=1\nSTOCK=UNKNOWN\nGRAIN=MEDIUM\nHALATION=ON\n",
        "ENABLED=1\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\n",
        "ENABLED=1\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\nHALATION=ON\nENABLED=0\n",
        "ENABLED=1\nSTOCK=PORTRA_400;reboot\nGRAIN=MEDIUM\nHALATION=ON\n",
        "ENABLED=2\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\nHALATION=ON\n"
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        struct film_settings a = before;
        assert(film_settings_parse(&a, bad[i], (unsigned)strlen(bad[i])) < 0);
        assert(memcmp(&a, &before, sizeof a) == 0);
    }
    before = m.settings;
    assert(film_menu_set_stock(&m, 5) < 0);
    assert(memcmp(&m.settings, &before, sizeof before) == 0);
    puts("FILM_MENU_TESTS_OK persistence=80 presets=5 chord=repeatable hidden_input=untouched");
    return 0;
}

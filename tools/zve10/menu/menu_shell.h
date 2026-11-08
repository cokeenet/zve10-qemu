#ifndef MENU_SHELL_H
#define MENU_SHELL_H

#include <stddef.h>
#include <stdint.h>

#define MENU_WIDTH 640
#define MENU_HEIGHT 480
#define MENU_PIXEL_BYTES 2

enum menu_key {
    MENU_KEY_UP,
    MENU_KEY_DOWN,
    MENU_KEY_LEFT,
    MENU_KEY_RIGHT,
    MENU_KEY_ENTER,
    MENU_KEY_BACK
};

struct menu_state {
    unsigned category;
    unsigned open;
};

/* Start open on category 7; a null state is ignored. */
void menu_init(struct menu_state *state);

/* UP/DOWN cycle through 0..7. LEFT/RIGHT and open ENTER do nothing.
 * BACK closes; closed BACK/ENTER reopen, preserving the category.
 * Return 1 for a state change, 0 for a no-op or invalid state/key. */
int menu_key(struct menu_state *state, enum menu_key key);

/* count is in uint16_t pixels, not bytes. Write exactly 640*480 pixels,
 * row-major, no stride, numeric RGBA4444 (R high nibble, A low nibble).
 * Every pixel is opaque, including the closed black frame (0x000f).
 * Return 0 on success, -1 for a null pointer, invalid state or short buffer;
 * rejected calls leave the buffer untouched. State and pixels must not overlap. */
int menu_render(const struct menu_state *state, uint16_t *pixels, size_t count);

#endif

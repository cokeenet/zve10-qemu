#include "menu_shell.h"
#include "original_assets.h"

enum { CATEGORY_COUNT = 8, TAB_CROP_WIDTH = 48 };

struct menu_rect {
    int x, y, width, height;
};

static struct menu_rect layout_box(unsigned record)
{
    struct menu_rect box;

    box.x = (int16_t)(a7_layout[record][10] & 0xffffu);
    box.y = (int16_t)(a7_layout[record][10] >> 16);
    box.width = (int16_t)(a7_layout[record][11] & 0xffffu);
    box.height = (int16_t)(a7_layout[record][11] >> 16);
    return box;
}

static struct menu_rect clipped(int64_t x, int64_t y,
                                int64_t width, int64_t height)
{
    struct menu_rect box = { 0, 0, 0, 0 };
    int64_t right = x + width;
    int64_t bottom = y + height;

    if (width <= 0 || height <= 0 || right <= 0 || bottom <= 0
        || x >= MENU_WIDTH || y >= MENU_HEIGHT) {
        return box;
    }
    box.x = x < 0 ? 0 : (int)x;
    box.y = y < 0 ? 0 : (int)y;
    box.width = (right > MENU_WIDTH ? MENU_WIDTH : (int)right) - box.x;
    box.height = (bottom > MENU_HEIGHT ? MENU_HEIGHT : (int)bottom) - box.y;
    return box;
}

static void rectangle(uint16_t *pixels, struct menu_rect box, uint16_t color)
{
    int y, x;

    box = clipped(box.x, box.y, box.width, box.height);
    color = (uint16_t)((color & 0xfff0u) | 0x000fu);
    for (y = box.y; y < box.y + box.height; ++y) {
        for (x = box.x; x < box.x + box.width; ++x) {
            pixels[(size_t)y * MENU_WIDTH + (unsigned)x] = color;
        }
    }
}

static uint16_t blend(uint16_t destination, uint16_t source, unsigned alpha)
{
    unsigned inverse = 255u - alpha;
    unsigned red = (((source >> 12) & 15u) * alpha
                    + ((destination >> 12) & 15u) * inverse + 127u) / 255u;
    unsigned green = (((source >> 8) & 15u) * alpha
                      + ((destination >> 8) & 15u) * inverse + 127u) / 255u;
    unsigned blue = (((source >> 4) & 15u) * alpha
                     + ((destination >> 4) & 15u) * inverse + 127u) / 255u;

    return (uint16_t)((red << 12) | (green << 8) | (blue << 4) | 15u);
}

static void sprite(uint16_t *pixels, int x, int y, int height,
                   const struct a7_sprite *image)
{
    struct menu_rect box;
    int row, column;
    unsigned width = image->width < TAB_CROP_WIDTH
        ? image->width : TAB_CROP_WIDTH;

    if (height <= 0 || image->pixels == NULL
        || (uint64_t)image->width * image->height > SIZE_MAX / sizeof(uint16_t)) {
        return;
    }
    if (image->height < (unsigned)height) {
        height = (int)image->height;
    }
    box = clipped(x, y, width, height);
    for (row = box.y; row < box.y + box.height; ++row) {
        size_t source_row = (size_t)(row - y) * image->width;
        size_t destination_row = (size_t)row * MENU_WIDTH;

        for (column = box.x; column < box.x + box.width; ++column) {
            uint16_t value = image->pixels[source_row + (size_t)(column - x)];
            size_t index = destination_row + (unsigned)column;

            /* Multiplying alpha4 by 17 preserves its exact blend ratio. */
            pixels[index] = blend(pixels[index], value, (value & 15u) * 17u);
        }
    }
}

static void text(uint16_t *pixels, struct menu_rect box,
                 const struct a7_text *image, int right_aligned)
{
    int64_t x = (int64_t)box.x
        + (right_aligned ? (int64_t)box.width - image->width : 0);
    int64_t y = (int64_t)box.y + ((int64_t)box.height - image->height) / 2;
    int row, column;

    if (image->alpha == NULL
        || (uint64_t)image->width * image->height > SIZE_MAX) {
        return;
    }
    box = clipped(box.x, box.y, box.width, box.height);
    for (row = box.y; row < box.y + box.height; ++row) {
        int64_t source_y = (int64_t)row - y;
        size_t source_row;

        if (source_y < 0 || (uint64_t)source_y >= image->height) {
            continue;
        }
        source_row = (size_t)source_y * image->width;
        for (column = box.x; column < box.x + box.width; ++column) {
            int64_t source_x = (int64_t)column - x;
            size_t index = (size_t)row * MENU_WIDTH + (unsigned)column;

            if (source_x >= 0 && (uint64_t)source_x < image->width) {
                pixels[index] = blend(pixels[index], a7_text_color,
                                      image->alpha[source_row + (size_t)source_x]);
            }
        }
    }
}

void menu_init(struct menu_state *state)
{
    if (state != NULL) {
        state->category = 7;
        state->open = 1;
    }
}

int menu_key(struct menu_state *state, enum menu_key key)
{
    if (state == NULL || state->category >= CATEGORY_COUNT || state->open > 1u) {
        return 0;
    }
    if (state->open == 0u) {
        if (key == MENU_KEY_BACK || key == MENU_KEY_ENTER) {
            state->open = 1;
            return 1;
        }
        return 0;
    }
    switch (key) {
    case MENU_KEY_UP:
        state->category = (state->category + CATEGORY_COUNT - 1u) % CATEGORY_COUNT;
        return 1;
    case MENU_KEY_DOWN:
        state->category = (state->category + 1u) % CATEGORY_COUNT;
        return 1;
    case MENU_KEY_BACK:
        state->open = 0;
        return 1;
    default:
        return 0;
    }
}

int menu_render(const struct menu_state *state, uint16_t *pixels, size_t count)
{
    struct menu_rect frame = { 0, 0, MENU_WIDTH, MENU_HEIGHT };
    struct menu_rect tabs;
    int row_height;
    unsigned i;

    if (state == NULL || pixels == NULL || count < (size_t)MENU_WIDTH * MENU_HEIGHT
        || state->category >= CATEGORY_COUNT || state->open > 1u) {
        return -1;
    }
    rectangle(pixels, frame, state->open ? a7_header_color : 0x000f);
    if (state->open == 0u) {
        return 0;
    }
    rectangle(pixels, layout_box(0), a7_header_color);
    rectangle(pixels, layout_box(1), a7_header_color);
    tabs = layout_box(3);
    rectangle(pixels, tabs, a7_background_color);
    row_height = tabs.height / CATEGORY_COUNT;
    for (i = 0; i < CATEGORY_COUNT; ++i) {
        const struct a7_sprite *image = i == state->category
            ? &a7_tab_selected[i] : &a7_tab_normal[i];

        sprite(pixels, tabs.x, tabs.y + (int)i * row_height, row_height, image);
    }
    text(pixels, layout_box(6), &a7_titles[state->category], 0);
    text(pixels, layout_box(8), &a7_page_text, 1);
    text(pixels, layout_box(7), &a7_footer_text, 1);
    return 0;
}

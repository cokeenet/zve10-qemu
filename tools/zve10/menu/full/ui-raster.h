/* Explicit CPU subset of the ARM32 DMP ABI in evidence/gpe-abi/REPORT.md. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint16_t ui_u16(const void *v) { uint16_t n; memcpy(&n, v, 2); return n; }
static uint32_t ui_u32(const void *v) { uint32_t n; memcpy(&n, v, 4); return n; }
static int ui_s16(const void *v) { return (int16_t)ui_u16(v); }
static unsigned ui_bpp(unsigned f) { return f == 0 ? 2 : (f == 3 ? 1 : (f == 1 || f == 7 ? 4 : 0)); }
static int ui_surface(const unsigned char *p, size_t *span)
{
    unsigned w = ui_u16(p + 4), h = ui_u16(p + 6), bpp = ui_bpp(ui_u32(p + 16));
    if (!w || !h || w > 8192 || h > 8192 || !bpp || ui_u32(p + 20)) return -5;
    *span = (size_t)w * h * bpp;
    return *span <= 64 * 1024 * 1024 ? 0 : -5;
}
static void ui_pixel_get(const unsigned char *p, unsigned f, unsigned c[4])
{
    if (f == 0) {
        unsigned n = ui_u16(p);
        for (int i = 0; i < 4; ++i) c[i] = ((n >> (12 - i * 4)) & 15) * 17;
    } else if (f == 3) {
        c[0] = c[1] = c[2] = 255; c[3] = p[0];
    } else {
        for (int i = 0; i < 4; ++i) c[i] = p[f == 1 ? 3 - i : i];
    }
}
static void ui_pixel_put(unsigned char *p, unsigned f, const unsigned c[4])
{
    if (f == 0) {
        uint16_t n = 0;
        for (int i = 0; i < 4; ++i) n |= (c[i] >> 4) << (12 - i * 4);
        memcpy(p, &n, 2);
    } else {
        for (int i = 0; i < 4; ++i) p[f == 1 ? 3 - i : i] = c[i];
    }
}
static unsigned ui_scale(unsigned x, unsigned a) { return (x * a + 127) / 255; }
static void ui_composite(unsigned char *dst, unsigned fmt, unsigned c[4], unsigned opacity, unsigned mode)
{
    unsigned old[4], out[4];
    ui_pixel_get(dst, fmt, old);
    unsigned factor = mode ? ui_scale(c[3], opacity) : opacity;
    for (int i = 0; i < 3; ++i) out[i] = ui_scale(c[i], factor) + ui_scale(old[i], 255 - factor);
    out[3] = ui_scale(c[3], opacity) + ui_scale(old[3], 255 - factor);
    for (int i = 0; i < 4; ++i) if (out[i] > 255) out[i] = 255;
    ui_pixel_put(dst, fmt, out);
}
static int ui_fill(unsigned char *dst, size_t capacity, const unsigned char *p)
{
    size_t span;
    unsigned fmt = ui_u32(p + 16), bpp = ui_bpp(fmt), sw = ui_u16(p + 4), sh = ui_u16(p + 6);
    int x = ui_s16(p + 8), y = ui_s16(p + 10), w = ui_s16(p + 12), h = ui_s16(p + 14);
    unsigned mode = ui_u32(p + 36);
    if (ui_surface(p, &span) || capacity < span || fmt == 3 || w <= 0 || h <= 0 || mode > 1 || ui_u32(p + 24)) return -5;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)sw ? (int)sw : x + w, y1 = y + h > (int)sh ? (int)sh : y + h;
    for (int j = y0; j < y1; ++j) for (int i = x0; i < x1; ++i) {
        unsigned c[4] = {p[28],p[29],p[30],p[31]};
        ui_composite(dst + ((size_t)j * sw + i) * bpp, fmt, c, p[32], mode);
    }
    return 0;
}
static int ui_blit(unsigned char *dst, size_t dc, const unsigned char *src, size_t sc, const unsigned char *p)
{
    size_t ss, ds;
    const unsigned char *d = p + 24;
    unsigned sf = ui_u32(p + 16), df = ui_u32(d + 24), sb = ui_bpp(sf), db = ui_bpp(df);
    /* Destination surface's format/layout are at +0x30/+0x34, unlike source +0x10/+0x14. */
    unsigned char descriptor[24];
    memcpy(descriptor, d, 8); memcpy(descriptor + 16, p + 48, 8);
    if (ui_surface(p, &ss) || ui_surface(descriptor, &ds) || dc < ds || sc < ss || df == 3) return -5;
    unsigned sw = ui_u16(p + 4), sh = ui_u16(p + 6), dw = ui_u16(d + 4), dh = ui_u16(d + 6);
    int sx = ui_s16(p + 8), sy = ui_s16(p + 10), rw = ui_s16(p + 12), rh = ui_s16(p + 14);
    int dx = ui_s16(p + 32), dy = ui_s16(p + 34), ow = ui_s16(p + 36), oh = ui_s16(p + 38);
    int cx = ui_s16(p + 40), cy = ui_s16(p + 42), cw = ui_s16(p + 44), ch = ui_s16(p + 46);
    unsigned flags = ui_u32(p + 144), mode = ui_u32(p + 68);
    /* shortcut: no tiled/linear-sampled, pivoted, rotated or inverted surfaces; add only from captured fixtures. */
    if (sx < 0 || sy < 0 || rw <= 0 || rh <= 0 || sx + rw > (int)sw || sy + rh > (int)sh ||
        ow <= 0 || oh <= 0 || cw <= 0 || ch <= 0 || mode > 1 || (flags & ~15u) || ui_u32(p + 56) ||
        ui_u32(p + 72) || ui_u32(p + 76) || ui_u16(p + 80) || (sf == 0 && df != 0 && ui_u32(p + 140) != 0x3f800000)) return -5;
    int x0 = dx > cx ? dx : cx, y0 = dy > cy ? dy : cy;
    int x1 = dx + ow < cx + cw ? dx + ow : cx + cw, y1 = dy + oh < cy + ch ? dy + oh : cy + ch;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)dw) x1 = dw;
    if (y1 > (int)dh) y1 = dh;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        int ix = sx + (int)((int64_t)(x - dx) * rw / ow), iy = sy + (int)((int64_t)(y - dy) * rh / oh);
        unsigned c[4]; ui_pixel_get(src + ((size_t)iy * sw + ix) * sb, sf, c);
        for (int i = 0; i < 4; ++i) if (flags & (8u >> i)) c[i] = p[61 + i];
        ui_composite(dst + ((size_t)y * dw + x) * db, df, c, p[60], mode);
    }
    return 0;
}

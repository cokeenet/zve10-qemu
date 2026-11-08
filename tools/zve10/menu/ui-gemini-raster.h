/* Original Gemini worker geometry: evidence/gemini-draw/REPORT.md.
 * Include after ui-raster.h; format numbers here are DMP (RGBA4444=0, A8=3),
 * not BitmapImpl or Gemini enums. All coordinates are top-left except scissor.
 */
#ifndef UI_GEMINI_RASTER_H
#define UI_GEMINI_RASTER_H
#include <math.h>

enum { UI_GEMINI_LINEAR, UI_GEMINI_MORTON_BOTTOM_UP, UI_GEMINI_MORTON_TOP_DOWN };
enum { UI_GEMINI_TEXTURE, UI_GEMINI_MULTIPLY, UI_GEMINI_ALPHA_MASK };
struct ui_gemini_rect { int16_t x, y, w, h; };
struct ui_gemini_surface {
    unsigned char *pixels;
    size_t capacity;
    unsigned width, height, stride, format, layout; /* stride in bytes */
};
struct ui_gemini_state {
    struct ui_gemini_rect viewport, scissor; /* scissor is native GL bottom-origin */
    uint32_t blend; /* native S+0x30: low RGB / high alpha */
    unsigned blend_enabled, scissor_enabled, rgb_write, alpha_write;
};

static inline int ui_gemini_span(const struct ui_gemini_surface *s, size_t *span)
{
    if (!s || !s->pixels || !s->width || !s->height || s->width > 8192 ||
        s->height > 8192 || (s->format != 0 && s->format != 3) ||
        s->layout > UI_GEMINI_MORTON_TOP_DOWN) return -5;
    unsigned bpp = s->format == 0 ? 2 : 1, rows = s->height;
    if (s->stride < s->width * bpp || s->stride > 8192 * bpp) return -5;
    if (s->layout != UI_GEMINI_LINEAR) {
        if (s->stride % (8*bpp) || (s->format==3 && s->layout!=UI_GEMINI_MORTON_TOP_DOWN)) return -5;
        rows = (rows + 7) & ~7u;
    }
    *span = (size_t)s->stride * rows;
    if (*span > 64u * 1024 * 1024 || s->capacity < *span ||
        (uintptr_t)s->pixels > UINTPTR_MAX - *span) return -5;
    return 0;
}

static inline size_t ui_gemini_offset(const struct ui_gemini_surface *s, unsigned x, unsigned y)
{
    if (s->layout == UI_GEMINI_LINEAR)
        return (size_t)y * s->stride + x * (s->format == 0 ? 2 : 1);
    if (s->layout == UI_GEMINI_MORTON_BOTTOM_UP) y = s->height - 1 - y;
    unsigned z = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
                 ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    unsigned bpp=s->format==0 ? 2 : 1;
    return (((size_t)(y >> 3) * (s->stride / (8*bpp)) + (x >> 3)) * 64 + z) * bpp;
}

static inline int ui_gemini_blend_valid(unsigned b)
{
    return !(b & ~0x70ffu) && (b >> 12) <= 2 &&
           ((b >> 4) & 15) <= 9 && (b & 15) <= 9;
}

static inline unsigned ui_gemini_factor(unsigned f, unsigned channel,
                                       const unsigned s[4], const unsigned d[4])
{
    switch (f) {
    case 0: return 0;
    case 1: return 255;
    case 2: return s[channel];
    case 3: return 255 - s[channel];
    case 4: return d[channel];
    case 5: return 255 - d[channel];
    case 6: return s[3];
    case 7: return 255 - s[3];
    case 8: return d[3];
    default: return 255 - d[3]; /* validated factor 9 */
    }
}

/* First pixel whose center is at/after edge; clamp before converting to int. */
static inline int ui_gemini_edge(double edge, unsigned limit)
{
    if (edge <= 0) return 0;
    if (edge >= limit) return (int)limit;
    int n = (int)edge;
    return n + (n + 0.5 < edge);
}

/* Internal rectangle implementation, also used for the native float quad ABI.
 * shortcut: positive axis-aligned affine and nearest in-bounds sampling only;
 * extend from actual captures when another transform/filter/address mode occurs.
 * Texture mode is explicit: caller must validate the live shader mapping.
 * Return 0 on synchronous success (including fully clipped), -5 without writes.
 */
static inline int ui_gemini_draw(struct ui_gemini_surface *dst,
                                 const struct ui_gemini_surface *src,
                                 const struct ui_gemini_state *state,
                                 const float matrix[16], const unsigned char color[4],
                                 const struct ui_gemini_rect *sr, unsigned mode,
                                 double x, double y, double w, double h)
{
    size_t ds, ss = 0;
    if (ui_gemini_span(dst, &ds) || dst->format != 0 || !state || !matrix || !color ||
        !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w <= 0 || h <= 0 ||
        state->viewport.w <= 0 || state->viewport.h <= 0 ||
        state->blend_enabled > 1 || state->scissor_enabled > 1 ||
        state->rgb_write > 1 || state->alpha_write > 1 ||
        !ui_gemini_blend_valid(state->blend & 65535) ||
        !ui_gemini_blend_valid(state->blend >> 16) ||
        (state->scissor_enabled && (state->scissor.w < 0 || state->scissor.h < 0))) return -5;
    for (unsigned i = 0; i < 16; ++i) if (!isfinite(matrix[i])) return -5;
    if (matrix[0] <= 0 || matrix[5] <= 0 || matrix[1] != 0 || matrix[4] != 0) return -5;
    if (src) {
        if (ui_gemini_span(src, &ss) || !sr || sr->x < 0 || sr->y < 0 || sr->w <= 0 || sr->h <= 0 ||
            (unsigned)(sr->x + sr->w) > src->width || (unsigned)(sr->y + sr->h) > src->height ||
            mode > UI_GEMINI_ALPHA_MASK ||
            (src->format == 3 ? mode != UI_GEMINI_ALPHA_MASK : mode == UI_GEMINI_ALPHA_MASK)) return -5;
        uintptr_t d = (uintptr_t)dst->pixels, s = (uintptr_t)src->pixels;
        if (d <= s ? s - d < ds : d - s < ss) return -5;
    } else if (sr) return -5;
    double left = x * matrix[0] + matrix[12] + state->viewport.x;
    double top = y * matrix[5] + matrix[13] + state->viewport.y;
    double right = (x + w) * matrix[0] + matrix[12] + state->viewport.x;
    double bottom = (y + h) * matrix[5] + matrix[13] + state->viewport.y;
    if (!isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom) ||
        right <= left || bottom <= top) return -5;
    int x0 = ui_gemini_edge(left, dst->width), x1 = ui_gemini_edge(right, dst->width);
    int y0 = ui_gemini_edge(top, dst->height), y1 = ui_gemini_edge(bottom, dst->height);
    int cx = state->viewport.x, cy = state->viewport.y;
    int cr = cx + state->viewport.w, cb = cy + state->viewport.h;
    if (state->scissor_enabled) {
        int sx = state->scissor.x, sy = (int)dst->height - state->scissor.y - state->scissor.h;
        int srgt = sx + state->scissor.w, sbot = sy + state->scissor.h;
        if (cx < sx) cx = sx;
        if (cy < sy) cy = sy;
        if (cr > srgt) cr = srgt;
        if (cb > sbot) cb = sbot;
    }
    if (x0 < cx) x0 = cx;
    if (y0 < cy) y0 = cy;
    if (x1 > cr) x1 = cr;
    if (y1 > cb) y1 = cb;
    for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px) {
        unsigned c[4] = {color[0], color[1], color[2], color[3]}, old[4], out[4];
        if (src) {
            unsigned ix = (unsigned)(((px + 0.5 - left) / (right - left)) * sr->w);
            unsigned iy = (unsigned)(((py + 0.5 - top) / (bottom - top)) * sr->h);
            /* Clamp the last texel against floating-point endpoint rounding. */
            if (ix >= (unsigned)sr->w) ix = sr->w - 1;
            if (iy >= (unsigned)sr->h) iy = sr->h - 1;
            ui_pixel_get(src->pixels + ui_gemini_offset(src, sr->x + ix, sr->y + iy), src->format, c);
            if (mode == UI_GEMINI_ALPHA_MASK) {
                for (unsigned i = 0; i < 3; ++i) c[i] = color[i];
                c[3] = ui_scale(c[3], color[3]);
            } else if (mode == UI_GEMINI_MULTIPLY) {
                for (unsigned i = 0; i < 4; ++i) c[i] = ui_scale(c[i], color[i]);
            }
        }
        unsigned char *p = dst->pixels + ui_gemini_offset(dst, (unsigned)px, (unsigned)py);
        ui_pixel_get(p, 0, old);
        for (unsigned i = 0; i < 4; ++i) {
            unsigned b = (state->blend >> (i == 3 ? 16 : 0)) & 65535;
            int v = (int)c[i];
            if (state->blend_enabled) {
                int a = (int)c[i] * (int)ui_gemini_factor((b >> 4) & 15, i, c, old);
                int d = (int)old[i] * (int)ui_gemini_factor(b & 15, i, c, old);
                int n = (b >> 12) == 0 ? a + d : ((b >> 12) == 1 ? a - d : d - a);
                v = n <= 0 ? 0 : (n + 127) / 255;
                if (v > 255) v = 255;
            }
            out[i] = (i == 3 ? state->alpha_write : state->rgb_write) ? (unsigned)v : old[i];
        }
        /* Match ui-raster's nibble truncation; GPU sub-nibble rounding is unverified. */
        ui_pixel_put(p, 0, out);
    }
    return 0;
}

static inline int ui_gemini_rect(struct ui_gemini_surface *dst,
                                 const struct ui_gemini_surface *src,
                                 const struct ui_gemini_state *state,
                                 const float matrix[16], const unsigned char color[4],
                                 const struct ui_gemini_rect *sr,
                                 const struct ui_gemini_rect *dr, unsigned mode)
{
    if (!dr) return -5;
    return ui_gemini_draw(dst, src, state, matrix, color, sr, mode, dr->x, dr->y, dr->w, dr->h);
}

static inline int ui_gemini_quad(struct ui_gemini_surface *dst,
                                 const struct ui_gemini_surface *src,
                                 const struct ui_gemini_state *state,
                                 const float matrix[16], const unsigned char color[4],
                                 const struct ui_gemini_rect *sr, const float points[8], unsigned mode)
{
    if (!src || !points) return -5;
    for (unsigned i = 0; i < 8; ++i) if (!isfinite(points[i])) return -5;
    /* Native input order TL, BL, BR, TR, emitted as 0,1,3,2. */
    if (points[0] != points[2] || points[4] != points[6] ||
        points[1] != points[7] || points[3] != points[5]) return -5;
    return ui_gemini_draw(dst, src, state, matrix, color, sr, mode,
                          points[0], points[1], (double)points[6] - points[0],
                          (double)points[3] - points[1]);
}

static inline int ui_slow_clear_pixels(unsigned char *pixels,size_t span,const uint32_t scissor[4])
{
    if (!scissor || scissor[0]>640 || scissor[1]>480 || scissor[2]>640-scissor[0] ||
        scissor[3]>480-scissor[1]) return -5;
    if (!scissor[2] || !scissor[3]) return 0;
    struct ui_gemini_surface dst={pixels,span,640,480,1280,0,UI_GEMINI_MORTON_BOTTOM_UP};
    struct ui_gemini_state state={{0,0,640,480},{0},0,0,0,1,1};
    struct ui_gemini_rect rect={(int16_t)scissor[0],(int16_t)(480-scissor[1]-scissor[3]),
                              (int16_t)scissor[2],(int16_t)scissor[3]};
    const float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const unsigned char color[4]={0};
    return ui_gemini_rect(&dst,NULL,&state,matrix,color,NULL,&rect,0);
}
#endif

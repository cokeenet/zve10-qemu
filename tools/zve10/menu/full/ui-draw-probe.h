#ifndef UI_DRAW_PROBE_H
#define UI_DRAW_PROBE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* Include after record(), object_base and struct hook; install only before UI starts. */
typedef uintptr_t (*gemini_probe_fn5)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*gemini_probe_fn6)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static gemini_probe_fn5 gemini_original_solid;
static gemini_probe_fn6 gemini_original_rect, gemini_original_quad, gemini_original_line;
static unsigned gemini_probe_count;
static unsigned gemini_kind_count[4];
static inline void inspect_gemini_counts(void)
{
    record("GEMINI.total.solid",gemini_kind_count[0]);
    record("GEMINI.total.textrect",gemini_kind_count[1]);
    record("GEMINI.total.quad",gemini_kind_count[2]);
    record("GEMINI.total.line",gemini_kind_count[3]);
}

static void gemini_probe_mapping(uintptr_t address)
{
    if (address < 4096 || gemini_probe_count > 3) return;
    int fd = open("/proc/self/pagemap",O_RDONLY | O_CLOEXEC);
    if (fd < 0) { record("GEMINI.mapping.open_error",errno); return; }
    uint64_t entry;
    ssize_t length = pread(fd,&entry,sizeof entry,(off_t)((address / 4096) * 8));
    close(fd);
    record("GEMINI.mapping.virtual",address);
    if (length != (ssize_t)sizeof entry) { record("GEMINI.mapping.read_error",(uintptr_t)length); return; }
    record("GEMINI.mapping.flags",(uintptr_t)(entry >> 55));
    uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
    if ((entry & (UINT64_C(1) << 63)) && !(entry & (UINT64_C(1) << 62)) && pfn < 0x40000)
        record("GEMINI.mapping.physical",(uintptr_t)(pfn * 4096 + (address & 4095)));
}

static uint32_t gemini_probe_read(uintptr_t p, unsigned offset)
{
    uint32_t value;
    memcpy(&value, (const void *)(p + offset), sizeof value);
    return value;
}

static void gemini_probe_field(const char *name, unsigned offset, uintptr_t value)
{
    char key[80];
    int n = snprintf(key, sizeof key, "%s+%03x", name, offset);
    if (n > 0 && n < (int)sizeof key) record(key, value);
}

static void gemini_probe_words(const char *name, uintptr_t p, unsigned words)
{
    record(name, p);
    if (p < 4096) return;
    for (unsigned i = 0; i < words; ++i)
        gemini_probe_field(name, i * 4, gemini_probe_read(p, i * 4));
}

static void gemini_probe_bitmap(const char *name, uintptr_t bitmap)
{
    static const unsigned offsets[] = {4, 8, 0x14, 0x18, 0x1c, 0x24, 0x2c, 0x30};
    record(name, bitmap);
    if (bitmap < 4096) return;
    uint32_t vptr = gemini_probe_read(bitmap, 0);
    gemini_probe_field(name, 0, vptr);
    if (!object_base || vptr != object_base + 0x13ba5a0) return;
    gemini_probe_field(name,0x18,gemini_probe_read(bitmap,0x18));
    gemini_probe_field(name,0x1c,gemini_probe_read(bitmap,0x1c));
    gemini_probe_field(name,0x20,gemini_probe_read(bitmap,0x20));
    uintptr_t meta = gemini_probe_read(bitmap, 4);
    gemini_probe_field(name, 4, meta);
    gemini_probe_field(name, 8, gemini_probe_read(bitmap, 8));
    gemini_probe_mapping(gemini_probe_read(bitmap, 8));
    if (meta < 4096) return;
    char key[80];
    int n = snprintf(key, sizeof key, "%s.meta", name);
    if (n <= 0 || n >= (int)sizeof key) return;
    record(key, meta);
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
        gemini_probe_field(key, offsets[i], gemini_probe_read(meta, offsets[i]));
    /* BMP enum, not Gemini's texture-format enum. No guest lock/getter calls. */
    static const unsigned char bpp[] = {0, 2, 4, 2, 1, 2, 2, 2, 4, 4};
    uint32_t format = gemini_probe_read(meta, 0x1c);
    if (format > 0 && format < sizeof bpp) {
        unsigned width = gemini_probe_read(meta, 0x18) & 0xffff;
        n = snprintf(key, sizeof key, "%s.stride", name);
        if (n > 0 && n < (int)sizeof key) record(key, width * bpp[format]);
    }
}

static void gemini_probe_texture(const char *name, uintptr_t texture, const char *bitmap_name)
{
    static const unsigned offsets[] = {0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c,
                                       0x34, 0x38, 0x3c, 0x40, 0x44, 0x48};
    record(name, texture);
    if (texture < 4096) return;
    uint32_t vptr = gemini_probe_read(texture, 0);
    gemini_probe_field(name, 0, vptr);
    if (!object_base || vptr != object_base + 0x13bf150) return;
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
        gemini_probe_field(name, offsets[i], gemini_probe_read(texture, offsets[i]));
    gemini_probe_bitmap(bitmap_name, gemini_probe_read(texture, 0x48));
}

static void gemini_probe_native(uintptr_t context)
{
    uintptr_t egl_context = gemini_probe_read(context, 0x170);
    uintptr_t surface = gemini_probe_read(context, 0x16c);
    /* EGL context/surface handle conversions are identity functions in this exact ELF. */
    if (egl_context >= 4096 && !(egl_context & 3)) {
        gemini_probe_field("GEMINI.EGLctx", 0x10, gemini_probe_read(egl_context, 0x10));
        uintptr_t holder = gemini_probe_read(egl_context, 0x2c);
        gemini_probe_field("GEMINI.EGLctx", 0x2c, holder);
        if (gemini_probe_read(egl_context, 0x10) == 2 && holder >= 4096 && !(holder & 3)) {
            uintptr_t gl = gemini_probe_read(holder, 0);
            record("GEMINI.GL", gl);
            if (gl >= 4096 && !(gl & 3)) {
                gemini_probe_field("GEMINI.GL", 0x6c4, gemini_probe_read(gl, 0x6c4));
                gemini_probe_field("GEMINI.GL", 0x6cc, gemini_probe_read(gl, 0x6cc));
                gemini_probe_field("GEMINI.GL", 0x6d0, gemini_probe_read(gl, 0x6d0));
            }
        }
    }
    if (surface >= 4096 && !(surface & 3)) {
        static const unsigned offsets[] = {8, 0x14, 0x18, 0x1c, 0x24, 0x30, 0x34,
                                           0x6c, 0x70, 0x78, 0x80, 0x8c, 0x90, 0x98};
        record("GEMINI.EGLsurface", surface);
        for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
            gemini_probe_field("GEMINI.EGLsurface", offsets[i], gemini_probe_read(surface, offsets[i]));
        /* Native window surface only; no inference from the global window's output buffer. */
        if (gemini_probe_read(surface, 8) == 1) {
            uintptr_t allocation = gemini_probe_read(surface, 0x78);
            gemini_probe_words("GEMINI.color.MA", allocation, 7);
        }
    }
}

static void gemini_probe_enter(const char *kind, uintptr_t renderer, uintptr_t context,
                               uintptr_t matrix, uintptr_t rgba, uintptr_t first,
                               uintptr_t second, unsigned first_words, unsigned second_words)
{
    unsigned ticket;
    do {
        ticket = __sync_fetch_and_add(&gemini_probe_count, 0);
        if (ticket >= 24) return;
    } while (!__sync_bool_compare_and_swap(&gemini_probe_count, ticket, ticket + 1));
    record(kind, ticket + 1);
    record("GEMINI.renderer", renderer);
    record("GEMINI.S", context);
    if (context >= 4096) {
        uint32_t vptr = gemini_probe_read(context, 0);
        record("GEMINI.S.vptr", vptr);
        if (object_base && vptr == object_base + 0x13bf2e0) {
            static const unsigned offsets[] = {0x2c, 0x30, 0x34, 0x58, 0x5c, 0x60, 0x64,
                                               0xa8, 0xac, 0xb8, 0x148,
                                               0x164, 0x168, 0x16c, 0x170, 0x174};
            for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
                gemini_probe_field("GEMINI.S", offsets[i], gemini_probe_read(context, offsets[i]));
            gemini_probe_native(context);
            gemini_probe_texture("GEMINI.src.T", gemini_probe_read(context, 0xb8), "GEMINI.src.bmp");
            uintptr_t target = gemini_probe_read(context, 0x148);
            record("GEMINI.RT", target);
            if (target >= 4096) {
                uint32_t target_vptr = gemini_probe_read(target, 0);
                record("GEMINI.RT.vptr", target_vptr);
                if (target_vptr == object_base + 0x13bf9b8)
                    gemini_probe_texture("GEMINI.dst.T", gemini_probe_read(target, 0x10), "GEMINI.dst.bmp");
            }
            /* Default GL framebuffer is internal tiled storage, not the window's output bitmap. */
        }
    }
    record("GEMINI.matrix", matrix);
    if (matrix >= 4096) {
        static const unsigned offsets[] = {0, 4, 0x10, 0x14, 0x30, 0x34};
        for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
            gemini_probe_field("GEMINI.matrix", offsets[i], gemini_probe_read(matrix, offsets[i]));
    }
    gemini_probe_words("GEMINI.rgba", rgba, 1);
    gemini_probe_words("GEMINI.first", first, first_words);
    if (second_words) gemini_probe_words("GEMINI.second", second, second_words);
}

static uintptr_t gemini_probe_solid(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t dst)
{
    ++gemini_kind_count[0];
    gemini_probe_enter("GEMINI.solid", r, s, m, c, dst, 0, 2, 0);
    return gemini_original_solid(r, s, m, c, dst);
}
static uintptr_t gemini_probe_rect(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t src, uintptr_t dst)
{
    ++gemini_kind_count[1];
    gemini_probe_enter("GEMINI.textrect", r, s, m, c, src, dst, 2, 2);
    return gemini_original_rect(r, s, m, c, src, dst);
}
static uintptr_t gemini_probe_quad(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t src, uintptr_t points)
{
    ++gemini_kind_count[2];
    gemini_probe_enter("GEMINI.quad", r, s, m, c, src, points, 2, 8);
    return gemini_original_quad(r, s, m, c, src, points);
}
static uintptr_t gemini_probe_line(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t first, uintptr_t second)
{
    ++gemini_kind_count[3];
    gemini_probe_enter("GEMINI.line", r, s, m, c, first, second, 2, 2);
    return gemini_original_line(r, s, m, c, first, second);
}

/* Eight bytes are complete PC-independent instructions; resume at entry+8 in Thumb state. */
#define GEMINI_PROBE_HOOKS \
    {0x848478, {0x2d,0xe9,0xf0,0x47,0x2d,0xed,0x04,0x8b}, (void *)gemini_probe_solid, &gemini_original_solid}, \
    {0x848160, {0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x06,0x8b}, (void *)gemini_probe_rect, &gemini_original_rect}, \
    {0x847e78, {0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x06,0x8b}, (void *)gemini_probe_quad, &gemini_original_quad}, \
    {0x8485fc, {0x2d,0xe9,0xf0,0x4f,0x83,0xb0,0x02,0xaf}, (void *)gemini_probe_line, &gemini_original_line}

#endif

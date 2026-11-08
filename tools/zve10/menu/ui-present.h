/* Captured native present/clear ABI: evidence/gpe-type6/ and gpe-type3/REPORT.md. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UI_PRESENT_BYTES (640u * 480u * 2u)

static inline size_t ui_present_span(const void *params)
{
    uint32_t p[14];
    if (!params) return 0;
    memcpy(p, params, sizeof p);
    if (p[0] < 0x20000000u || p[0] > 0x28000000u - UI_PRESENT_BYTES ||
        p[1] < 0x20000000u || p[1] > 0x28000000u - UI_PRESENT_BYTES ||
        ((uint64_t)p[0] + UI_PRESENT_BYTES > p[1] &&
         (uint64_t)p[1] + UI_PRESENT_BYTES > p[0])) return 0;
    /* shortcut: only the captured full RGBA4444 present; decode other layouts before extending. */
    const uint32_t expected[12] = {0,4,0,0x4401,640,480,640,480,0,0,640,480};
    return memcmp(p + 2, expected, sizeof expected) ? 0 : UI_PRESENT_BYTES;
}

static inline int ui_present_copy(unsigned char *dst, size_t ds,
                                  const unsigned char *src, size_t ss, const void *params)
{
    size_t span = ui_present_span(params);
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    if (!span || !dst || !src || ds < span || ss < span ||
        (d <= s ? s - d < span : d - s < span)) return -5;
    for (unsigned y = 0; y < 480; ++y) {
        unsigned sy = 479 - y;
        for (unsigned x = 0; x < 640; ++x) {
            unsigned tile = (sy >> 3) * 80 + (x >> 3);
            unsigned z = (x & 1) | ((sy & 1) << 1) | ((x & 2) << 1) |
                         ((sy & 2) << 2) | ((x & 4) << 2) | ((sy & 4) << 3);
            size_t from = (tile * 64 + z) * 2, to = (y * 640 + x) * 2;
            dst[to] = src[from];
            dst[to + 1] = src[from + 1];
        }
    }
    return 0;
}

static inline size_t ui_range_clear_span(const void *record)
{
    uint32_t p[19];
    if (!record) return 0;
    memcpy(p,record,sizeof p);
    if (p[1] != 3 || p[17] || p[18] != 0x0f100001 || (p[15] & 7) || (p[16] & 7) ||
        p[15] < 0x20000000u || p[16] > 0x28000000u || p[16] <= p[15] ||
        p[16] - p[15] != UI_PRESENT_BYTES) return 0;
    return UI_PRESENT_BYTES;
}

static inline int ui_range_clear(unsigned char *dst, size_t ds, const void *record)
{
    size_t span = ui_range_clear_span(record);
    if (!span || !dst || ds < span) return -5;
    memset(dst,0,span);
    return 0;
}

static inline uint32_t ui_present_take_completion(long *pending_tid, long tid, unsigned engine)
{
    if (engine != 2 || !*pending_tid || *pending_tid != tid) return 0;
    *pending_tid = 0;
    return 0x40000000u;
}

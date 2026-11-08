#ifndef ZVE10_UI_HEADLESS_H
#define ZVE10_UI_HEADLESS_H

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Test seams only; production calls the verified original ARM32 entries. */
#ifndef UI_HEADLESS_FN
#define UI_HEADLESS_FN(offset, type) ((type)(object_base + (offset)))
#endif
#ifndef UI_HEADLESS_PATH
#define UI_HEADLESS_PATH "/tmp/zve10-headless.json"
#endif

enum { HEADLESS_BYTES = 640 * 480 * 2, HEADLESS_MAX_PAGES = 151 };
struct headless_segment { uint64_t physical; size_t offset, length; };
struct headless_snapshot {
    unsigned char *pixels;
    uint32_t bitmap, physical, key;
    unsigned index, segments;
    uint64_t frame, fingerprint;
    unsigned char descriptor[32];
    struct headless_segment page[HEADLESS_MAX_PAGES];
};
static struct headless_snapshot headless_retained;
static uint64_t headless_native_frames;
static unsigned native_glyph_draws, native_glyph_images;
static uintptr_t native_glyph_bitmaps[64];
static int headless_published;

static void headless_fail(const char *phase, uintptr_t detail)
{
    if (headless_published) unlink(UI_HEADLESS_PATH);
    record(phase, detail);
    _exit(71);
}
static uint32_t headless_u32(const void *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}
static uint16_t headless_u16(const void *p)
{
    uint16_t value;
    memcpy(&value, p, sizeof value);
    return value;
}
static uint64_t headless_fingerprint(const unsigned char *pixels, size_t length)
{
    uint64_t value = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < length; ++i)
        value = (value ^ pixels[i]) * UINT64_C(0x100000001b3);
    return value;
}
static int headless_nonuniform(const unsigned char *pixels)
{
    uint16_t first = headless_u16(pixels);
    for (size_t i = 2; i < HEADLESS_BYTES; i += 2)
        if (headless_u16(pixels + i) != first) return 1;
    return 0;
}

static void headless_copy(struct headless_snapshot *s, const unsigned char *r)
{
    uintptr_t bitmap = headless_u32(r + 28);
    if (!bitmap || (bitmap & 3) || bitmap > UINT32_MAX - 128 ||
        headless_u32((void *)bitmap) != object_base + 0x13ba5a0)
        headless_fail("headless.error.bitmap", bitmap);
    uintptr_t metadata = headless_u32((void *)(bitmap + 4));
    if (!metadata || (metadata & 3) || metadata > UINT32_MAX - 0x34)
        headless_fail("headless.error.metadata", metadata);
    unsigned char m[0x34];
    memcpy(m, (void *)metadata, sizeof m);
    int16_t bpp = 0;
    uint32_t physical = 0;
    if (headless_u32(m + 0x1c) != 1 ||
        headless_u16(m + 4) || headless_u16(m + 6) ||
        headless_u16(m + 8) != 640 || headless_u16(m + 10) != 480 ||
        headless_u16(m + 0x14) || headless_u16(m + 0x16) ||
        headless_u16(m + 0x18) != 640 || headless_u16(m + 0x1a) != 480 ||
        headless_u32(m + 0x2c) || headless_u32(m + 0x30) ||
        headless_u16(r + 8) != 480 || headless_u16(r + 10) != 640 ||
        headless_u16(r + 12) != 2 ||
        UI_HEADLESS_FN(0x69d145, int (*)(uint32_t, int16_t *))(1, &bpp) || bpp != 2 ||
        UI_HEADLESS_FN(0x684e6d, int (*)(void *, uint32_t *))((void *)bitmap, &physical) ||
        physical != headless_u32(m + 0x24) || physical != headless_u32(r + 20) ||
        physical < 0x20000000u || physical > 0x28000000u - HEADLESS_BYTES)
        headless_fail("headless.error.layout_identity", bitmap);
    s->pixels = malloc(HEADLESS_BYTES);
    if (!s->pixels) headless_fail("headless.error.allocate", HEADLESS_BYTES);
    const void *bits = NULL;
    int status = UI_HEADLESS_FN(0x69b901, int (*)(void *, int32_t, const void **))((void *)bitmap, 0, &bits);
    if (status) headless_fail("headless.error.read_lock", (uintptr_t)(intptr_t)status);
    int valid = bits && (uintptr_t)bits == headless_u32((void *)(bitmap + 8)) &&
        !memcmp(m, (void *)metadata, sizeof m);
    if (valid) memcpy(s->pixels, bits, HEADLESS_BYTES);
    valid = valid && !memcmp(m, (void *)metadata, sizeof m) &&
        (uintptr_t)bits == headless_u32((void *)(bitmap + 8));
    UI_HEADLESS_FN(0x69b29d, void (*)(void *))((void *)bitmap);
    if (!valid) headless_fail("headless.error.changed_identity", bitmap);
    s->bitmap = (uint32_t)bitmap;
    s->physical = physical;
    s->key = headless_u32(r + 24);
    memcpy(s->descriptor, r, sizeof s->descriptor);
}

static void headless_pages(struct headless_snapshot *s)
{
    if (sysconf(_SC_PAGESIZE) != 4096)
        headless_fail("headless.error.page_size", 0);
    int fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (fd < 0) headless_fail("headless.error.pagemap_open", errno);
    s->segments = 0;
    for (size_t offset = 0; offset < HEADLESS_BYTES;) {
        uintptr_t address = (uintptr_t)s->pixels + offset;
        uint64_t entry = 0;
        size_t read_bytes = 0;
        off_t position = (off_t)((address / 4096) * 8);
        while (read_bytes < sizeof entry) {
            ssize_t n = pread(fd, (unsigned char *)&entry + read_bytes,
                              sizeof entry - read_bytes, position + (off_t)read_bytes);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { close(fd); headless_fail("headless.error.pagemap_read", errno); }
            read_bytes += (size_t)n;
        }
        uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
        uint64_t physical = pfn * 4096 + (address & 4095);
        size_t length = 4096 - (address & 4095);
        if (length > HEADLESS_BYTES - offset) length = HEADLESS_BYTES - offset;
        if (!(entry & (UINT64_C(1) << 63)) || (entry & (UINT64_C(1) << 62)) ||
            pfn >= 0x40000u || physical > 0x40000000u - length ||
            s->segments == HEADLESS_MAX_PAGES) {
            close(fd);
            headless_fail("headless.error.pagemap_identity", (uintptr_t)physical);
        }
        s->page[s->segments++] = (struct headless_segment){physical, offset, length};
        offset += length;
    }
    if (close(fd)) headless_fail("headless.error.pagemap_close", errno);
}

static void headless_publish(const struct headless_snapshot *s, uint64_t frame, unsigned count)
{
    char part[512];
    int path_length = snprintf(part, sizeof part, "%s.part", UI_HEADLESS_PATH);
    if (path_length < 0 || path_length >= (int)sizeof part)
        headless_fail("headless.error.export_path", 0);
    int fd = open(part, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) headless_fail("headless.error.export_open", errno);
    FILE *out = fdopen(fd, "w");
    if (!out) { close(fd); unlink(part); headless_fail("headless.error.export_stream", errno); }
    int ok = fprintf(out, "{\"label\":\"committed_native_frame\",\"pid\":%ld,"
        "\"native_frame_count\":%" PRIu64 ",\"native_record_count\":%u,"
        "\"export_ready\":%s,\"retained\":%s",
        (long)getpid(), frame, count, s->pixels ? "true" : "false", s->pixels ? "true" : "false") >= 0;
    if (s->pixels) {
        ok &= fprintf(out, ",\"capture_frame_count\":%" PRIu64 ",\"record_index\":%u,"
            "\"bitmap\":%" PRIu32 ",\"source_physical\":%" PRIu32 ",\"framebuffer_key\":%" PRIu32 ","
            "\"width\":640,\"height\":480,\"format\":1,\"bpp\":2,\"stride\":1280,"
            "\"sorting\":0,\"start_position\":0,\"byte_length\":%u,"
            "\"capture_pointer\":%" PRIuPTR ",\"fingerprint_fnv1a64\":\"%016" PRIx64 "\",\"record_hex\":\"",
            s->frame, s->index, s->bitmap, s->physical, s->key,
            (unsigned)HEADLESS_BYTES, (uintptr_t)s->pixels,s->fingerprint) >= 0;
        for (unsigned i = 0; i < sizeof s->descriptor; ++i)
            ok &= fprintf(out, "%02x", s->descriptor[i]) >= 0;
        ok &= fprintf(out, "\",\"segments\":[") >= 0;
        for (unsigned i = 0; i < s->segments; ++i)
            ok &= fprintf(out, "%s{\"physical\":%" PRIu64 ",\"capture_offset\":%zu,\"length\":%zu}",
                i ? "," : "", s->page[i].physical, s->page[i].offset, s->page[i].length) >= 0;
        ok &= fprintf(out, "]") >= 0;
    }
    ok &= fprintf(out, "}\n") >= 0;
    if (fflush(out) || fsync(fd)) ok = 0;
    if (fclose(out)) ok = 0;
    if (!ok || rename(part, UI_HEADLESS_PATH)) {
        unlink(part);
        headless_fail("headless.error.export_commit", errno);
    }
    headless_published = 1;
}

static void headless_capture_line(const char *line, int length, size_t capacity)
{
    if (length <= 0 || (size_t)length >= capacity)
        headless_fail("headless.error.capture_format", 0);
    for (size_t offset = 0; offset < (size_t)length;) {
        ssize_t written = write(2,line + offset,(size_t)length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) headless_fail("headless.error.capture_write", errno);
        offset += (size_t)written;
    }
}
static void headless_export_segments(const struct headless_snapshot *s)
{
    char line[512];
    int length = snprintf(line,sizeof line,
        "UI_CAPTURE_META {\"frame\":%" PRIu64 ",\"record_index\":%u,\"bitmap\":%" PRIu32
        ",\"source_physical\":%" PRIu32 ",\"width\":640,\"height\":480,\"format\":1,\"bpp\":2,\"stride\":1280,"
        "\"byte_length\":%u,\"segments\":%u,\"fingerprint_fnv1a64\":\"%016" PRIx64 "\",\"committed_menu_frame\":false}\n",
        s->frame,s->index,s->bitmap,s->physical,(unsigned)HEADLESS_BYTES,s->segments,s->fingerprint);
    headless_capture_line(line,length,sizeof line);
    for (unsigned i = 0; i < s->segments; ++i) {
        length = snprintf(line,sizeof line,
            "UI_CAPTURE_PAGE {\"frame\":%" PRIu64 ",\"index\":%u,\"physical\":%" PRIu64
            ",\"capture_offset\":%zu,\"length\":%zu}\n",
            s->frame,i,s->page[i].physical,s->page[i].offset,s->page[i].length);
        headless_capture_line(line,length,sizeof line);
    }
}

/* Producer fence/cache completion is upstream; a bitmap read lock is not a GPU fence. */
static uintptr_t software_headless_update(uintptr_t records, uintptr_t count,
                                          uintptr_t unused2, uintptr_t unused3)
{
    (void)unused2; (void)unused3;
    if (!object_base || count < 1 || count > 2 ||
        records != object_base + 0x145d0f0 + 0xb0 ||
        headless_u32((void *)(object_base + 0x145d0f0 + 0x5c)) != count ||
        headless_u32((void *)(object_base + 0x145dbc0)) != object_base + 0x6f6cf7 ||
        headless_u32((void *)(object_base + 0x145dbb8)) != object_base + 0x145d0f0)
        headless_fail("headless.error.transaction", records);
    uint32_t sid = headless_u32((void *)(object_base + 0x145dbc4));
    int (*wait_sem)(uint32_t, uint32_t) = dlsym(RTLD_NEXT, "osal_wai_sem_tmo");
    if (!sid || !wait_sem) headless_fail("headless.error.gate", sid);
    int status = wait_sem(sid, UINT32_MAX);
    if (status) headless_fail("headless.error.gate_wait", (uintptr_t)(intptr_t)status);
    struct headless_snapshot copies[2] = {{0}};
    int selected = -1;
    uint64_t frame = headless_native_frames + 1;
    for (unsigned i = 0; i < count; ++i) {
        copies[i].index = i;
        copies[i].frame = frame;
        headless_copy(&copies[i], (const unsigned char *)records + 32 * i);
        /* shortcut: title plus six name/value pairs; verify coverage before capturing another menu shape. */
        if (0 && native_glyph_images>=13 && !headless_retained.pixels && selected < 0 && headless_nonuniform(copies[i].pixels)) {
            /* Pin the retained private heap pages so published PFNs survive later frames. */
            if (mlock(copies[i].pixels, HEADLESS_BYTES)) headless_fail("headless.error.pin", errno);
            copies[i].fingerprint = headless_fingerprint(copies[i].pixels,HEADLESS_BYTES);
            headless_pages(&copies[i]);
            selected = (int)i;
        }
    }
    ne_transport_cache(copies,(unsigned)count,frame);
    const struct headless_snapshot *export = selected >= 0 ? &copies[selected] : &headless_retained;
    headless_publish(export, frame, (unsigned)count);
    if (selected >= 0) {
        headless_retained = copies[selected];
        headless_export_segments(&headless_retained);
    }
    for (unsigned i = 0; i < count; ++i)
        if ((int)i != selected) free(copies[i].pixels);
    headless_native_frames = frame;
    /* This alone signals the acquired gate, retires references, and unlocks the manager. */
    UI_HEADLESS_FN(0x6fafb9, void (*)(void))();
    ne_transport_retired(frame);
    if (frame <= 8) {
        char output[256];
        int length=snprintf(output,sizeof output,
            "UI_OUTPUT_COMMITTED {\"frame\":%" PRIu64 ",\"physical\":%" PRIu32 ",\"bitmap\":%" PRIu32 ",\"width\":640,\"height\":480,\"bpp\":2,\"byte_length\":%u}\n",
            frame,copies[0].physical,copies[0].bitmap,(unsigned)HEADLESS_BYTES);
        headless_capture_line(output,length,sizeof output);
    }
    if (frame <= 8 || !(frame & (frame - 1))) {
        record("headless.committed_native_frame", (uintptr_t)frame);
        record("headless.export_ready", headless_retained.pixels != NULL);
    }
    if (selected >= 0) {
        char line[128];
        int length = snprintf(line,sizeof line,"UI_CAPTURE_READY {\"frame\":%" PRIu64 ",\"segments\":%u}\n",
                              headless_retained.frame,headless_retained.segments);
        headless_capture_line(line,length,sizeof line);
    }
    return 0;
}

#endif

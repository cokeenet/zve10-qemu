#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ui-raster.h"
#include "ui-gemini-raster.h"
#include "ui-present.h"
#include "ui-lz77.h"
#include <pthread.h>
#include "native_entry.h"

/* Cold-start hooks only: install before IMDB invokes the UI lifecycle entries. */
typedef uintptr_t (*entry_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static entry_fn original_notify, original_draw, original_application, original_font_register;
static entry_fn original_rm_execute, original_rm_step, original_engine_wait, original_lz_decode;
static unsigned lz_decode_calls;
static long present_completed_tid;
static unsigned rm_calls, rm_steps;
static unsigned notify_count, draw_count;
static int installed;
static unsigned raster_count, raster_failed;
static int memory_fd = -1;
static uintptr_t object_base, views_base;
static int menu_attempted;
static long ui_tid, rm_tid;
static unsigned output_messages;
static void try_original_menu(void);
static void inspect_native_buffers(void);
static void inspect_native_views(void);
static void software_range_clear(uint32_t *record);
static void gemini_probe_mapping(uintptr_t address);
static void inspect_gemini_counts(void);
static void inspect_font_counts(void);

static void record(const char *name, uintptr_t value)
{
    char line[160];
    int length = snprintf(line, sizeof line, "UI_OBSERVE pid=%d tid=%ld %s=%#lx\n", getpid(), syscall(224), name, (unsigned long)value);
    if (length > 0 && length < (int)sizeof line)
        write(2, line, length);
}

static uintptr_t observe_notify(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    if (++notify_count <= 8) record("notifyDraw.enter", a);
    uintptr_t result = original_notify(a, b, c, d);
    if (notify_count <= 8) record("notifyDraw.return", a);
    return result;
}
static uintptr_t observe_draw(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    ui_tid = syscall(224);
    if (++draw_count <= 8) record("drawFramebuffer.enter", a);
    if (draw_count <= 4) inspect_native_views();
    if (draw_count <= 2) {
        record("FONT.driver",ui_u32((void *)(object_base+0x145cd94)));
        record("FONT.textrm.initialized",ui_u32((void *)(object_base+0x145c964)));
        for (unsigned i=0;i<8;++i)
            record("FONT.loaded.slot",ui_u32((void *)(object_base+0x145cd14+i*4)));
    }
    uintptr_t result = original_draw(a, b, c, d);
    if (draw_count <= 8) {
        record("drawFramebuffer.return", a);
        inspect_gemini_counts();
        inspect_font_counts();
    }
    if (draw_count <= 2) inspect_native_buffers();
    try_original_menu();
    native_entry_after_draw();
    return result;
}
static uintptr_t observe_application(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    record("Application_System_Init.enter", a);
    uintptr_t result = original_application(a, b, c, d);
    record("Application_System_Init.return", result);
    return result;
}

static uintptr_t observe_font_register(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    record("FONT.register.id",a);
    record("FONT.register.path",b);
    uintptr_t result=original_font_register(a,b,c,d);
    record("FONT.register.return",result);
    return result;
}

static uintptr_t observe_lz_decode(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    const unsigned char *decoder = (void *)a;
    unsigned ticket = __sync_add_and_fetch(&lz_decode_calls,1);
    unsigned width = ui_u16(decoder + 16), height = ui_u16(decoder + 18);
    unsigned length = ui_u32(decoder + 20);
    int trace = ticket <= 8 || (width == 64 && height == 2);
    if (trace) {
        record("LDEC.decode.enter",a);
        record("LDEC.decode.format",ui_u32(decoder + 12));
        record("LDEC.decode.dimensions",ui_u32(decoder + 16));
        record("LDEC.decode.source_length",length);
        uintptr_t input = ui_u32(decoder + 4);
        if (input >= 4096 && length && length <= 64 * 1024 * 1024) {
            uintptr_t vptr = ui_u32((void *)input);
            uintptr_t getter = vptr >= 4096 ? ui_u32((void *)(vptr + 0x2c)) : 0;
            if (getter >= 4096 && getter < 0x60000000) {
                uintptr_t buffer = ((uintptr_t (*)(uintptr_t))getter)(input);
                record("LDEC.decode.source",buffer);
                if (buffer >= 4096 && buffer < 0x60000000 && length <= 0x60000000 - buffer) {
                    for (unsigned i = 0; i + 4 <= length && i < 64; i += 4)
                        record("LDEC.decode.word",ui_u32((void *)(buffer + i)));
                    Dl_info info;
                    if (dladdr((void *)buffer,&info) && info.dli_fbase) {
                        record("LDEC.decode.library_base",(uintptr_t)info.dli_fbase);
                        record("LDEC.decode.library_offset",buffer - (uintptr_t)info.dli_fbase);
                    }
                }
            }
        }
    }
    uintptr_t result = original_lz_decode(a,b,c,d);
    if (trace) record("LDEC.decode.return",result);
    return result;
}

#include "ui-image-decode.h"

static void inspect_native_views(void)
{
    if (!object_base) return;
    uintptr_t manager = *(uint32_t *)(object_base + 0x149221c);
    if (!manager || ui_u32((void *)manager) != 43) return;
    if (!ui_u32((void *)(manager + 8)) || !ui_u32((void *)(manager + 12)) ||
        !ui_u32((void *)(manager + 16)) || !ui_u32((void *)(manager + 28))) return;
    const uint32_t *slots = (void *)(uintptr_t)ui_u32((void *)(manager + 32));
    if (!slots) return;
    for (unsigned i = 0; i < 43; ++i) {
        uintptr_t view=slots[i*4+1];
        unsigned id=slots[i*4+2];
        if (!view) continue;
        record("native.view.id",id);
        if (id==0x11 || id==0xba) {
            record("native.menu.view",view);
            record("native.menu.vptr",ui_u32((void *)view));
            record("native.menu.state",ui_u32((void *)(view+4)));
            if (id==0xba && views_base && ui_u32((void *)view)==views_base+0xbaabb0) {
                uintptr_t widget=((uintptr_t (*)(uintptr_t,uint32_t))(object_base+0x1398e9))(view,0xa056f604);
                uintptr_t list=widget ? ((uintptr_t (*)(uintptr_t))(views_base+0x861c29))(widget) : 0;
                record("native.setting.list",list);
                if (list>=4096 && list<0x60000000u-0x186bc) {
                    record("native.setting.list.tree",ui_u32((void *)(list+0x186ac)));
                    record("native.setting.list.flag",*(unsigned char *)(list+0x1d8));
                    for (unsigned off=0x1e4;off<=0x200;off+=4) {
                        char label[48];snprintf(label,sizeof label,"native.setting.list+%03x",off);
                        record(label,ui_u32((void *)(list+off)));
                    }
                }
                for (unsigned off=0x154;off<=0x1dc;off+=4) {
                    char label[48];
                    snprintf(label,sizeof label,"native.setting+%03x",off);
                    record(label,ui_u32((void *)(view+off)));
                }
                uintptr_t tree=ui_u32((void *)(view+0x1b8));
                if (tree>=4096 && tree<0x60000000u-0x30) {
                    for (unsigned off=0;off<0x30;off+=4) {
                        char label[48];snprintf(label,sizeof label,"native.setting.tree+%02x",off);
                        record(label,ui_u32((void *)(tree+off)));
                    }
                    uintptr_t vptr=ui_u32((void *)tree);
                    Dl_info info;
                    if (dladdr((void *)vptr,&info) && info.dli_fbase) {
                        record("native.setting.tree.library_base",(uintptr_t)info.dli_fbase);
                        record("native.setting.tree.vptr_offset",vptr-(uintptr_t)info.dli_fbase);
                    }
                }
            }
        }
    }
}

static void inspect_native_buffers(void)
{
    if (!object_base) return;
    uint32_t *wrapper = (void *)(uintptr_t)*(uint32_t *)(object_base + 0x145cc68);
    if (!wrapper) return;
    unsigned char *window = (void *)(uintptr_t)wrapper[1];
    if (!window || ui_u32(window) != object_base + 0x13bbde0) return;
    for (unsigned group = 0; group < 2; ++group) {
        unsigned char *g = window + (group ? 0xc8 : 0x58);
        uintptr_t first = ui_u32(g + 0x58), last = ui_u32(g + 0x5c);
        unsigned selected = ui_u32(g + 0x64);
        if (!first || last < first || (last - first) % 8 || (last - first) / 8 > 16) continue;
        for (unsigned i = 0; i < (last - first) / 8; ++i) {
            uint32_t *bitmap = (void *)(uintptr_t)*(uint32_t *)(first + i * 8);
            if (!bitmap || bitmap[0] != object_base + 0x13ba5a0 || !bitmap[1]) continue;
            const unsigned char *m = (void *)(uintptr_t)bitmap[1];
            int16_t bytes_per_pixel = 0;
            int result = ((int (*)(uint32_t,int16_t *))(object_base + 0x69d145))(ui_u32(m + 0x1c),&bytes_per_pixel);
            if (result || bytes_per_pixel <= 0 || bytes_per_pixel > 4) continue;
            unsigned aw = ui_u16(m + 0x18), ah = ui_u16(m + 0x1a);
            uint64_t span = (uint64_t)aw * ah * bytes_per_pixel;
            if (!span || span > 64 * 1024 * 1024) continue;
            char line[700];
            int n = snprintf(line,sizeof line,
                "UI_BITMAP {\"group\":%u,\"index\":%u,\"selected\":%u,\"physical\":%u,\"bits\":%u,\"format\":%u,\"width\":%u,\"height\":%u,\"allocation_width\":%u,\"allocation_height\":%u,\"bpp\":%u,\"sorting\":%u,\"start_position\":%u,\"span\":%u}\n",
                group,i,selected,ui_u32(m + 0x24),bitmap[2],ui_u32(m + 0x1c),
                (unsigned)ui_u16(m + 8),(unsigned)ui_u16(m + 10),aw,ah,(unsigned)bytes_per_pixel,
                ui_u32(m + 0x2c),ui_u32(m + 0x30),(unsigned)span);
            if (n > 0 && n < (int)sizeof line) write(2,line,n);
            gemini_probe_mapping(bitmap[2]);
        }
    }
}

static uintptr_t observe_rm_execute(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    rm_tid = syscall(224);
    unsigned call = ++rm_calls;
    const uint32_t *request_words=(void *)a;
    int capture=call<=32 || ((request_words[1]==1 || request_words[1]==7 || request_words[1]==8) && request_words[17]!=4);
    if (capture) {
        record("RM.execute.enter", a);
        const uint32_t *p = (void *)a;
        for (unsigned i = 0; i < 32; ++i) record("RM.record", p[i]);
        if ((p[1] == 1 || p[1] == 7 || p[1] == 8) && p[17] >= 4 && p[15] >= 4096 && p[15] < 0x60000000) {
            const uint32_t *packet = (void *)(uintptr_t)p[15];
            unsigned words=p[17]<=256 ? p[17]*2 : 8;
            record("RM.packet.words",words);
            for (unsigned i = 0; i < words; ++i) record("RM.packet", packet[i]);
        }
    }
    uint32_t *request = (void *)a;
    uint32_t saved_flags = request[0];
    int cpu_clear = request[1] == 3 && !(saved_flags & 2);
    if (cpu_clear) {
        software_range_clear(request);
        request[0] = saved_flags | 2; /* Native skip-hardware path still retires callbacks and sync objects. */
    }
    uintptr_t result = original_rm_execute(a,b,c,d);
    if (cpu_clear) request[0] = saved_flags;
    if (capture) record("RM.execute.return", call);
    return result;
}
static uintptr_t observe_rm_step(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    unsigned call = ++rm_steps;
    if (call <= 32) record("RM.step.enter", call);
    uintptr_t result = original_rm_step(a,b,c,d);
    if (call <= 32) record("RM.step.return", result);
    return result;
}

static void try_original_menu(void)
{
    if (menu_attempted || !object_base || !views_base) return;
    uintptr_t manager = *(uint32_t *)(object_base + 0x149221c);
    if (!manager || *(uint32_t *)manager != 43) return;
    if (!*(uint32_t *)(manager + 8) || !*(uint32_t *)(manager + 12) ||
        !*(uint32_t *)(manager + 16) || !*(uint32_t *)(manager + 28)) return;
    const uint32_t *slots = (void *)(uintptr_t)*(uint32_t *)(manager + 32);
    if (!slots) return;
    uintptr_t root = 0;
    for (unsigned i = 0; i < 43; ++i) {
        if (!slots[i * 4 + 1]) continue;
        unsigned id = slots[i * 4 + 2];
        if (id == 0x11 || id == 0x5a || id == 0xba) { menu_attempted = 1; return; }
        if (id == 0xa3) root = slots[i * 4 + 1];
    }
    if (!root || *(uint32_t *)root != views_base + 0xbacc50 ||
        *(uint32_t *)(root + 0x154) != views_base + 0xbace80 ||
        *(uint32_t *)(root + 0x24) != manager) return;
    menu_attempted = 1;
    record("native.menu.root", root);
    ((void (*)(int))(views_base + 0x590f35))(1);
    if (((unsigned (*)(void))(views_base + 0x5910b5))()) {
        record("native.menu.wifi_guard", 1);
        return;
    }
    ((void (*)(void *, int))(views_base + 0x47de27))((void *)(root + 0x154), 0);
    record("native.menu.request.return", 1);
}

struct surface_mapping {
    void *base;
    unsigned char *pixels;
    size_t length;
};
static int map_surface(uint32_t physical, size_t span, struct surface_mapping *m)
{
    if (physical < 0x20000000u || physical >= 0x28000000u || span > 0x28000000u - physical)
        return -5;
    if (memory_fd < 0) memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) return -5;
    unsigned delta = physical & 4095;
    m->length = (span + delta + 4095) & ~(size_t)4095;
    m->base = mmap(NULL, m->length, PROT_READ|PROT_WRITE, MAP_SHARED, memory_fd, physical & ~4095u);
    if (m->base == MAP_FAILED) return -5;
    m->pixels = (unsigned char *)m->base + delta;
    return 0;
}
static void software_range_clear(uint32_t *request)
{
    size_t span = ui_range_clear_span(request);
    struct surface_mapping dst;
    if (!span || map_surface(request[15],span,&dst)) {
        record("GPE.clear.UNSUPPORTED", request[15]);
        record("GPE.clear.end", request[16]);
        record("GPE.clear.pattern", request[17]);
        record("GPE.clear.control", request[18]);
        _exit(71);
    }
    int result = ui_range_clear(dst.pixels,span,request);
    __sync_synchronize();
    if (munmap(dst.base,dst.length) || result) {
        record("GPE.clear.error", result ? (uintptr_t)result : (uintptr_t)errno);
        _exit(71);
    }
    record("GPE.clear.start", request[15]);
    record("GPE.clear.cleared", span);
}

static uintptr_t software_present(uintptr_t params, uintptr_t b, uintptr_t c, uintptr_t d)
{
    (void)b; (void)c; (void)d;
    const unsigned char *p = (void *)params;
    size_t span = ui_present_span(p);
    if (present_completed_tid || !span) {
        record("GPE.present.UNSUPPORTED", params);
        if (p) for (unsigned i = 0; i < 14; ++i) record("GPE.present.param", ui_u32(p + i * 4));
        _exit(71);
    }
    struct surface_mapping src, dst;
    if (map_surface(ui_u32(p),span,&src)) {
        record("GPE.present.source.error", errno);
        _exit(71);
    }
    if (map_surface(ui_u32(p + 4),span,&dst)) {
        record("GPE.present.destination.error", errno);
        munmap(src.base,src.length);
        _exit(71);
    }
    int result = ui_present_copy(dst.pixels,span,src.pixels,span,p);
    if (!result) result = native_entry_present(dst.pixels,span,ui_u32(p + 4),(uintptr_t)__builtin_return_address(0));
    __sync_synchronize();
    int unmap_error = munmap(dst.base,dst.length);
    unmap_error |= munmap(src.base,src.length);
    if (result || unmap_error) {
        record("GPE.present.copy.error", result ? (uintptr_t)result : (uintptr_t)errno);
        _exit(71);
    }
    present_completed_tid = syscall(224);
    record("GPE.present.source", ui_u32(p));
    record("GPE.present.destination", ui_u32(p + 4));
    record("GPE.present.copied", span);
    return 0;
}
static uintptr_t software_engine_wait(uintptr_t engine, uintptr_t b, uintptr_t c, uintptr_t d)
{
    uint32_t status = ui_present_take_completion(&present_completed_tid,syscall(224),(unsigned)engine);
    if (status) {
        record("GPE.present.completed", status);
        return status;
    }
    return original_engine_wait(engine,b,c,d);
}

static void dump_params(const char *label, const unsigned char *p, unsigned length)
{
    record(label, ++raster_count);
    for (unsigned i = 0; i < length; i += 4) record("DMP.param", ui_u32(p + i));
}
static uintptr_t software_init(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    record("DMP.cpu.initialize", 1);
    return 0;
}
static uintptr_t software_exit(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    record("DMP.cpu.exit", raster_failed);
    return 0;
}
static uintptr_t software_fill(uintptr_t request, uintptr_t mode, uintptr_t params, uintptr_t unused)
{
    (void)unused;
    const unsigned char *p = (void *)params;
    size_t span;
    struct surface_mapping m;
    int status = -5;
    if (!p || mode > 2) return (uintptr_t)-5;
    if (raster_count < 8) dump_params("DMP.fill", p, 48);
    if (!ui_surface(p, &span) && !map_surface(ui_u32(p), span, &m)) {
        status = ui_fill(m.pixels, span, p);
        munmap(m.base, m.length);
    }
    if (status) { ++raster_failed; record("DMP.fill.UNSUPPORTED", raster_failed); return (uintptr_t)status; }
    if (ui_u32(p + 40)) ((void (*)(uint32_t, uint32_t))(uintptr_t)ui_u32(p + 40))(request, ui_u32(p + 44));
    return 0;
}
static uintptr_t software_blit(uintptr_t request, uintptr_t mode, uintptr_t params, uintptr_t unused)
{
    (void)unused;
    const unsigned char *p = (void *)params;
    size_t ss, ds;
    struct surface_mapping src, dst;
    int status = -5;
    if (!p || mode > 2) return (uintptr_t)-5;
    if (raster_count < 8) dump_params("DMP.blit", p, 156);
    unsigned char desc[24] = {0};
    memcpy(desc, p + 24, 8); memcpy(desc + 16, p + 48, 8);
    if (!ui_surface(p, &ss) && !ui_surface(desc, &ds)) {
        uint32_t s = ui_u32(p), d = ui_u32(p + 24);
        if ((uint64_t)s + ss <= d || (uint64_t)d + ds <= s) {
            if (!map_surface(s, ss, &src)) {
                if (!map_surface(d, ds, &dst)) {
                    status = ui_blit(dst.pixels, ds, src.pixels, ss, p);
                    munmap(dst.base, dst.length);
                }
                munmap(src.base, src.length);
            }
        }
    }
    if (status) { ++raster_failed; record("DMP.blit.UNSUPPORTED", raster_failed); return (uintptr_t)status; }
    if (ui_u32(p + 148)) ((void (*)(uint32_t, uint32_t))(uintptr_t)ui_u32(p + 148))(request, ui_u32(p + 152));
    return 0;
}

#include "ui-headless.h"

struct hook {
    uint32_t offset;
    unsigned char prologue[8];
    void *replacement;
    void *original;
};
#include "ui-draw-probe.h"
#include "ui-gemini-backend.h"
#include "ui-slow-clear.h"
#include "ui-font-probe.h"

static struct hook hooks[] = {
    {0x617c74, {0x90,0xb5,0x87,0xb0,0x00,0xaf,0x04,0x46}, observe_notify, &original_notify},
    {0x6196b8, {0xb0,0xb5,0x04,0x46,0x82,0xb0,0x00,0xaf}, observe_draw, &original_draw},
    {0x8a2408, {0xb0,0xb5,0x8a,0xb0,0x02,0xaf,0x05,0x46}, observe_application, &original_application},
    {0x7d5bb0, {0x03,0x48,0x80,0xb4,0x78,0x44,0x00,0xaf}, software_init, NULL},
    {0x7d5bd0, {0x03,0x48,0x80,0xb4,0x78,0x44,0x00,0xaf}, software_exit, NULL},
    {0x7d5bf0, {0x02,0x29,0x0b,0x46,0xf0,0xb5,0x85,0xb0}, software_fill, NULL},
    {0x7d5c80, {0x02,0x29,0x0b,0x46,0xf0,0xb5,0x85,0xb0}, software_blit, NULL},
    {0x774ef0, {0x2d,0xe9,0xf0,0x41,0x84,0xb0,0x05,0x46}, software_present, NULL},
    {0x775120, {0xf0,0xb5,0x83,0xb0,0x44,0x1e,0x00,0xaf}, software_engine_wait, &original_engine_wait},
    {0x6fb28c, {0xb0,0xb5,0x0c,0x46,0x86,0xb0,0x1c,0x49}, software_headless_update, NULL},
    {0x63e170, {0x2d,0xe9,0xf0,0x4f,0x8d,0xb0,0x00,0xaf}, observe_lz_decode, &original_lz_decode},
    {0x63cc58, {0x2d,0xe9,0xf0,0x41,0x82,0xb0,0x02,0xaf}, observe_font_register, &original_font_register},
    GEMINI_PROBE_HOOKS,
    FONT_PROBE_HOOKS,
};

static void thumb_jump(unsigned char *where, uintptr_t to)
{
    const unsigned char instruction[] = {0xdf,0xf8,0x00,0xf0}; /* ldr.w pc,[pc,#0] */
    memcpy(where, instruction, 4);
    uint32_t target = to;
    memcpy(where + 4, &target, 4);
}
static int install_rm_hook(uintptr_t base, uint32_t offset, const unsigned char expected[12], entry_fn replacement, entry_fn *original)
{
    unsigned char *address = (void *)(base + offset);
    if (memcmp(address, expected, 12)) { record("RM.prologue.unsupported", offset); return -1; }
    uint16_t load = ui_u16(address + 6);
    if ((load & 0xf800) != 0x4800) return -1;
    uint32_t literal = *(uint32_t *)(((uintptr_t)(address + 10) & ~(uintptr_t)3) + (load & 255) * 4);
    unsigned char *code = mmap(NULL, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return -1;
    if (mprotect((void *)((uintptr_t)address & ~(uintptr_t)4095),4096,PROT_READ|PROT_WRITE|PROT_EXEC)) {
        munmap(code,4096); return -1;
    }
    memcpy(code,address,12);
    uint16_t relocated = (load & 0xff00) | 3; /* pool at +20, PC at +8 */
    memcpy(code + 6,&relocated,2);
    thumb_jump(code + 12,((uintptr_t)address + 12) | 1);
    memcpy(code + 20,&literal,4);
    *original = (entry_fn)((uintptr_t)code | 1);
    __builtin___clear_cache((char *)code,(char *)code + 24);
    thumb_jump(address,(uintptr_t)replacement);
    __builtin___clear_cache((char *)address,(char *)address + 8);
    return 0;
}

static void install(void *handle)
{
    void *getter = dlsym(handle, "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    Dl_info info;
    if (!getter || !dladdr(getter, &info) || !info.dli_fbase || installed)
        return;
    uintptr_t base = (uintptr_t)info.dli_fbase;
    if (((uintptr_t)getter & ~(uintptr_t)1) - base != 0x616fe4)
        return;
    for (unsigned i = 0; i < sizeof hooks / sizeof hooks[0]; ++i) {
        if (memcmp((void *)(base + hooks[i].offset), hooks[i].prologue, 8)) {
            record("unsupported.prologue", hooks[i].offset);
            return;
        }
    }
    unsigned char *tramp = mmap(NULL, 4096, PROT_READ|PROT_WRITE|PROT_EXEC,
                                MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) { record("mmap.error", errno); return; }
    for (unsigned i = 0; i < sizeof hooks / sizeof hooks[0]; ++i) {
        uintptr_t address = base + hooks[i].offset;
        if (mprotect((void *)(address & ~(uintptr_t)4095), 4096, PROT_READ|PROT_WRITE|PROT_EXEC)) {
            record("mprotect.error", errno);
            munmap(tramp, 4096);
            return;
        }
        if (hooks[i].original) {
            memcpy(tramp + i * 16, (void *)address, 8);
            thumb_jump(tramp + i * 16 + 8, (address + 8) | 1);
            uintptr_t target = (uintptr_t)(tramp + i * 16) | 1;
            memcpy(hooks[i].original,&target,sizeof target);
        }
    }
    __builtin___clear_cache((char *)tramp, (char *)tramp + sizeof hooks / sizeof hooks[0] * 16);
    for (unsigned i = 0; i < sizeof hooks / sizeof hooks[0]; ++i) {
        unsigned char *address = (void *)(base + hooks[i].offset);
        thumb_jump(address, (uintptr_t)hooks[i].replacement);
        __builtin___clear_cache((char *)address, (char *)address + 8);
    }
    static const unsigned char executor[12] = {0x2d,0xe9,0xf0,0x4f,0x87,0xb0,0xc6,0x4d,0x04,0xaf,0x04,0x46};
    static const unsigned char scheduler[12] = {0x2d,0xe9,0xf0,0x41,0xa0,0xb0,0x83,0x4c,0x00,0xaf,0x38,0x46};
    if (!install_rm_hook(base,0x775260,executor,observe_rm_execute,&original_rm_execute) &&
        !install_rm_hook(base,0x776ad0,scheduler,observe_rm_step,&original_rm_step))
        record("RM.hooks.installed",1);
    static const unsigned char text_draw[12]={0x2d,0xe9,0xf0,0x4f,0xc5,0xb0,0xba,0x4e,0x06,0xaf,0x04,0x46};
    if (!install_rm_hook(base,0x572008,text_draw,observe_text_draw,&original_text_draw))
        record("FONT.raster.hook_installed",1);
    gemini_original_solid = software_gemini_solid;
    gemini_original_rect = software_gemini_rect;
    gemini_original_quad = software_gemini_quad;
    gemini_original_line = software_gemini_line;
    original_lz_decode = software_lz_decode;
    original_slow_clear = software_slow_clear;
    installed = 1;
    object_base = base;
    record("hooks.installed.base", base);
}

struct init_binding { void *function; char name[100]; };
static struct init_binding bindings[256];
static unsigned binding_count;
static void init_enter(unsigned index, uintptr_t argument)
{
    char line[240];
    int n = snprintf(line, sizeof line, "UI_INIT_ENTER pid=%d tid=%ld name=%s arg=%#lx\n",
                     getpid(), syscall(224), bindings[index].name, (unsigned long)argument);
    if (n > 0 && n < (int)sizeof line) write(2, line, n);
}
static uintptr_t run_initializer(uintptr_t argument, unsigned index)
{
    init_enter(index, argument);
    /* IMDB's loader calls each initializer with one configuration-string argument. */
    uintptr_t result = ((uintptr_t (*)(uintptr_t))bindings[index].function)(argument);
    char line[240];
    int n = snprintf(line, sizeof line, "UI_INIT_RETURN pid=%d tid=%ld name=%s result=%#lx\n",
                     getpid(), syscall(224), bindings[index].name, (unsigned long)result);
    if (n > 0 && n < (int)sizeof line) write(2, line, n);
    return result;
}
static void *bind_initializer(const char *name, void *function)
{
    unsigned index = __sync_fetch_and_add(&binding_count, 1);
    if (index >= 256) return function;
    uint32_t *stub = mmap(NULL, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (stub == MAP_FAILED) return function;
    bindings[index].function = function;
    snprintf(bindings[index].name, sizeof bindings[index].name, "%s", name);
    uint32_t words[] = {
        0xe3001000 | index, /* movw r1,index; preserve r0 configuration pointer */
        0xe51ff004, (uint32_t)(uintptr_t)run_initializer
    };
    memcpy(stub, words, sizeof words);
    __builtin___clear_cache((char *)stub, (char *)stub + sizeof words);
    return stub;
}

void *dlsym(void *handle, const char *name)
{
    static void *(*native_dlsym)(void *, const char *);
    if (!native_dlsym) native_dlsym = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.4");
    if (!native_dlsym) return NULL;
    void *result = native_dlsym(handle, name);
    if (strstr(name, "_init") || strstr(name, "Init")) {
        char line[240];
        int n = snprintf(line, sizeof line, "UI_RESOLVE pid=%d name=%s result=%p\n", getpid(), name, result);
        if (n > 0 && n < (int)sizeof line) write(2, line, n);
        if (result) return bind_initializer(name, result);
    }
    return result;
}

void *dlopen(const char *path, int flags)
{
    static void *(*native_dlopen)(const char *, int);
    if (!native_dlopen)
        native_dlopen = dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.4");
    if (!native_dlopen) { errno = ENOSYS; return NULL; }
    void *handle = native_dlopen(path, flags);
    if (!handle || !path) {
        char line[240];
        int n = snprintf(line, sizeof line, "UI_LOAD pid=%d path=%s flags=%#x result=%p\n", getpid(), path ? path : "<main>", flags, handle);
        if (n > 0 && n < (int)sizeof line) write(2, line, n);
    }
    if (handle && path && strstr(path, "libObj.so"))
        install(handle);
    if (handle && path && strstr(path, "viewUnified2.so")) {
        void *factory = dlsym(handle, "ViewRootSelectorToInstance");
        Dl_info info;
        if (factory && dladdr(factory, &info) &&
            (((uintptr_t)factory & ~(uintptr_t)1) - (uintptr_t)info.dli_fbase == 0x41b6a8)) {
            views_base = (uintptr_t)info.dli_fbase;
            record("native.menu.views.base", views_base);
            native_entry_modules(handle);
        }
    }
    return handle;
}

__attribute__((used,noinline)) static int ui_cond_wait_impl(void *condition, void *mutex, uint32_t *thumb_frame)
{
    static int (*native_wait)(void *, void *);
    if (!native_wait) native_wait = dlvsym(RTLD_NEXT, "pthread_cond_wait", "GLIBC_2.4");
    if (!native_wait) return ENOSYS;
    int trace = draw_count <= 8 && (syscall(224) == getpid() || syscall(224) == ui_tid || syscall(224) == rm_tid);
    if (trace) {
        uintptr_t caller = (uintptr_t)__builtin_return_address(0);
        record("thread.cond_wait.caller", caller);
        record("thread.cond_wait.condition", (uintptr_t)condition);
        if (object_base && (caller & ~(uintptr_t)1) == object_base + 0x7856e8 &&
            (uintptr_t)thumb_frame >= 4096 && (uintptr_t)thumb_frame < 0x60000000 && !((uintptr_t)thumb_frame & 3))
            record("thread.cond_wait.native_parent", thumb_frame[1]);
    }
    int result = native_wait(condition, mutex);
    if (trace) record("thread.cond_wait.return", result);
    return result;
}

__attribute__((naked)) int pthread_cond_wait(pthread_cond_t *condition __attribute__((unused)), pthread_mutex_t *mutex __attribute__((unused)))
{
    __asm__ __volatile__("mov r2, r7\n\tb ui_cond_wait_impl");
}

int pthread_join(unsigned long thread, void **value)
{
    static int (*native_join)(unsigned long, void **);
    if (!native_join) native_join = dlvsym(RTLD_NEXT, "pthread_join", "GLIBC_2.4");
    if (!native_join) return ENOSYS;
    record("thread.join.caller", (uintptr_t)__builtin_return_address(0));
    int result = native_join(thread, value);
    record("thread.join.return", result);
    return result;
}

int osal_wai_sem_tmo(uint32_t id, uint32_t timeout)
{
    static int (*native_wait)(uint32_t, uint32_t);
    if (!native_wait) native_wait = dlsym(RTLD_NEXT, "osal_wai_sem_tmo");
    if (!native_wait) return -1;
    int main = draw_count <= 8 && (syscall(224) == getpid() || syscall(224) == ui_tid || syscall(224) == rm_tid);
    if (main) {
        record("thread.osal_sem.caller", (uintptr_t)__builtin_return_address(0));
        record("thread.osal_sem.id", id);
    }
    int result = native_wait(id, timeout);
    if (main) record("thread.osal_sem.return", result);
    return result;
}

int osal_snd_msg(uint32_t queue, void *message)
{
    static int (*native_send)(uint32_t, void *);
    static int (*native_size)(void *);
    if (!native_send) native_send = dlsym(RTLD_NEXT, "osal_snd_msg");
    if (!native_size) native_size = dlsym(RTLD_NEXT, "osal_msg_size");
    if (!native_send) return -1;
    if (syscall(224) == ui_tid && output_messages++ < 32) {
        record("ui.send.queue", queue);
        int length = message && native_size ? native_size(message) : 0;
        record("ui.send.size", length);
        if (length >= 4 && length <= 4096) {
            unsigned max = length > 24 ? 24 : (unsigned)length;
            for (unsigned i = 0; i + 4 <= max; i += 4) record("ui.send.word", ui_u32((unsigned char *)message + i));
        }
    }
    return native_send(queue, message);
}

__attribute__((constructor)) static void loaded(void)
{
    record("library.loaded", 1);
}

#include "native_entry.inc"

/* ZV-E10 native menu probe — 最小、只读、可回滚。
 *
 * 目的：在做任何菜单之前，先把「架构是否成立」测出来。
 * 只用 libObj.so 已导出的函数，不 hook、不改字节、不冻结线程。
 *
 * 步骤：
 *   1. dlsym 拿 18 个 API
 *   2. WidgetSystem::sys_getInstance() 拿单例
 *   3. createLayer(singleton, genID("ZV_E10_PROBE")) 看是否非 NULL
 *   4. 若成功：建 1 个背景 Panel + 1 个 Label，setActive(1)，
 *      3 秒后 setActive(0)，全程写日志
 *
 * 全部结果写 /log/zv-e10-menu-probe.log。
 * 失败/异常不影响相机（没有 hook、没有 patch）。
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LOG "/log/zv-e10-menu-probe.log"

static FILE *g_log;

static void rec(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

typedef void (*ctor_fn)(void *);
typedef void (*unary_fn)(void *);
typedef void (*bool_fn)(void *, int);
typedef void (*layer_id_fn)(void *, const uint32_t *);
typedef void (*size_fn)(void *, float, float) __attribute__((pcs("aapcs")));
typedef void (*position_fn)(void *, const float *);
typedef void (*text_fn)(void *, const char *);
typedef void (*color_fn)(void *, const unsigned char *);
typedef uint32_t (*id_fn)(const char *);
typedef void *(*create_layer_fn)(void *, const uint32_t *);
typedef void *(*instance_fn)(void);

static struct {
    void *system, *layer, *bg, *title;
    uint32_t layer_id;
    ctor_fn widget_ctor, panel_ctor, label_ctor;
    layer_id_fn set_layer;
    position_fn set_pos;
    size_fn panel_size, label_size;
    text_fn set_text;
    color_fn set_color;
    bool_fn layer_active, layer_clear;
    unary_fn move_last;
    id_fn make_id;
    create_layer_fn create_layer;
    instance_fn get_instance;
} api;

static int resolve(void)
{
#define GET(m, name) do { api.m = (void *)dlsym(RTLD_DEFAULT, name); \
    if (!api.m) { rec("MISSING %s", name); return -1; } } while (0)
    GET(widget_ctor, "_ZN2ux6wgtsys6WidgetC1Ev");
    GET(panel_ctor,  "_ZN9GEN_PanelC1Ev");
    GET(label_ctor,  "_ZN9GEN_LabelC1Ev");
    GET(set_layer,   "_ZN2ux6wgtsys6Widget10setLayerIDERKNS_4core2IDE");
    GET(set_pos,     "_ZN2ux6wgtsys6Widget11setLocalPosERKNS_4core7Vector2E");
    GET(panel_size,  "_ZN9GEN_Panel7setSizeEff");
    GET(label_size,  "_ZN9GEN_Label7setSizeEff");
    GET(set_text,    "_ZN9GEN_Label13setTextDirectEPKc");
    GET(set_color,   "_ZN9GEN_Panel14setColorDirectERKN6stella6CColorE");
    GET(layer_active,"_ZN2ux6wgtsys5Layer9setActiveEb");
    GET(layer_clear, "_ZN2ux6wgtsys5Layer8setClearEb");
    GET(move_last,   "_ZN2ux6wgtsys9LayerBase8moveLastEv");
    GET(make_id,     "_ZN2ux4core2ID5genIDEPKc");
    GET(create_layer,"_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
    GET(get_instance,"_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
#undef GET
    return 0;
}

static void place(void *w, int label, int x, int y, int wd, int ht)
{
    const float xy[2] = { (float)x, (float)y };
    api.set_pos(w, xy);
    (label ? api.label_size : api.panel_size)(w, (float)wd, (float)ht);
}

int main(void)
{
    g_log = fopen(LOG, "a");
    if (!g_log) return 1;
    rec("=== PROBE START pid=%d tid=%ld ===", getpid(),
        (long)syscall(SYS_gettid));

    void *obj = dlopen("libObj.so", RTLD_LAZY);
    if (!obj) obj = dlopen("/usr/lib/libObj.so", RTLD_LAZY);
    if (!obj) { rec("dlopen libObj failed: %s", dlerror()); return 2; }
    rec("libObj handle=%p", obj);

    if (resolve()) { rec("RESOLVE FAILED"); return 3; }
    rec("all 15 APIs resolved");

    void *system = api.get_instance();
    rec("sys_getInstance = %p", system);
    if (!system) { rec("NULL singleton"); return 4; }

    api.layer_id = api.make_id("ZV_E10_MENU_PROBE");
    rec("genID = %#x", api.layer_id);

    api.layer = api.create_layer(system, &api.layer_id);
    rec("createLayer = %p", api.layer);
    if (!api.layer) { rec("createLayer returned NULL — 架构不成立或线程不对"); return 5; }

    api.layer_active(api.layer, 0);
    api.layer_clear(api.layer, 1);

    api.bg = calloc(1, 0x200);
    api.panel_ctor(api.bg);
    api.set_layer(api.bg, &api.layer_id);
    place(api.bg, 0, 0, 0, 640, 480);
    const unsigned char bg_color[4] = { 10, 14, 20, 255 };
    api.set_color(api.bg, bg_color);
    rec("background panel built");

    api.title = calloc(1, 0x400);
    api.label_ctor(api.title);
    api.set_layer(api.title, &api.layer_id);
    place(api.title, 1, 28, 24, 584, 48);
    api.set_text(api.title, "ZV-E10 MENU PROBE");
    rec("title label built");

    api.move_last(api.layer);
    api.layer_active(api.layer, 1);
    rec("LAYER ACTIVE - 屏幕应出现标题");

    sleep(3);

    api.layer_active(api.layer, 0);
    rec("LAYER HIDDEN");
    rec("=== PROBE OK ===");
    return 0;
}

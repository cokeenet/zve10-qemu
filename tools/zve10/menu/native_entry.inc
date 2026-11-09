#include "nm_log.h"
/* Included after the pinned original software-rendering observer. */
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include "menu_shell.h"
#include "native_pins.h"

struct ne_property { int32_t key, kind; uint32_t value; };
struct ne_properties { const struct ne_property *records; int32_t last; };
struct ne_node {
    uintptr_t vptr;
    struct ne_node **children;
    int32_t count;
    const struct ne_properties *properties;
    struct ne_node *parent;
    int32_t depth, selected, selectable, physical;
    uint32_t state;
};
_Static_assert(sizeof(uintptr_t) == 4, "ZVE10 native ARM32 ABI");
_Static_assert(sizeof(struct ne_node) == 0x28, "verified native base node size");
_Static_assert(sizeof(struct ne_properties) == 8, "verified properties size");
_Static_assert(sizeof(struct ne_property) == 12, "verified property stride");

typedef void (*ne_init_fn)(void *, void *, int);
typedef void (*ne_populate_fn)(void *);
typedef void (*ne_main_fn)(void *, int *, int *);
typedef uintptr_t (*ne_row_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                               uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static ne_init_fn ne_original_init;
static ne_populate_fn ne_original_populate;
static ne_main_fn ne_original_main;
static ne_row_fn ne_original_row;
static uintptr_t ne_caution;
static void *ne_view_handle;
static struct ne_properties ne_own_properties;
static struct ne_property ne_own_records[6];
static struct ne_node *ne_root, *ne_parent, *ne_added;
static struct ne_node **ne_original_children;
static uint32_t ne_installed, ne_title_sets, ne_entries, ne_exits;
static volatile uint32_t ne_packed, ne_last_input, ne_pending_input, ne_pending_function;
static int ne_modules_installed;
static uintptr_t ne_bound_view;
static uint64_t ne_bound_frame, ne_required_frame;
static uint32_t ne_overlay_physical;
static uint64_t ne_overlay_fingerprint;
static uint32_t ne_overlay_packed;
static pthread_mutex_t ne_present_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 日志走 nm_log（见 nm_log.h），它写 /log/newmenu.log */
static void ne_line(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    int size = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (size <= 0 || size >= (int)sizeof text) return;
    nm_log("%s", text);
}

static void *ne_find_symbol(const char *name)
{
    return ne_view_handle ? dlsym(ne_view_handle, name) : NULL;
}
static uintptr_t ne_pointer(uintptr_t address)
{
    uintptr_t value;
    memcpy(&value, (const void *)address, sizeof value);
    return value;
}
static struct ne_node *ne_find_version_parent(struct ne_node *node, struct ne_node *version,
                                               unsigned depth, unsigned *budget)
{
    if (!node || depth > 2 || !*budget) return NULL;
    --*budget;
    if (node->count < 0 || node->count > 256 || (node->count && !node->children)) return NULL;
    if (node->count == 5 && node->children[1] &&
            node->children[1]->properties == version->properties)
        return node;
    if (depth == 2) return NULL;
    for (int i = 0; i < node->count; ++i) {
        struct ne_node *found = ne_find_version_parent(node->children[i], version, depth + 1, budget);
        if (found) return found;
    }
    return NULL;
}
static int ne_add_row(void *root)
{
    struct ne_node *version = ne_find_symbol("cmnViewSettingNodeRootSetupVersion");
    ne_line("NE_VERSION {\"node\":%u,\"vptr\":%u,\"properties\":%u,\"last\":%d}\n",
            (unsigned)(uintptr_t)version, version ? (unsigned)version->vptr : 0,
            version ? (unsigned)(uintptr_t)version->properties : 0,
            version && version->properties ? version->properties->last : -1);
    if (!version || !version->properties || version->properties->last != 6) return -1;
    static const int32_t keys[6] = {0, 6, 11, 12, 14, 41};
    static const int32_t kinds[6] = {0, 0, 1, 1, 1, 1};
    static const uint32_t values[6] = {0x0204e12b, 0x0204e2c3, 0, 3, 0xde, 0x5c};
    for (unsigned i = 0; i < 6; ++i) {
        const struct ne_property *p = &version->properties->records[i];
        if (p->key != keys[i] || p->kind != kinds[i] || p->value != values[i]) return -2;
    }
    unsigned budget = 2048;
    struct ne_node *parent = ne_find_version_parent(root, version, 0, &budget);
    if (!parent || version->vptr != ne_caution + 0xfab968 || parent->vptr != version->vptr) return -3;
    void *(*allocate_array)(size_t) = ne_find_symbol("_Znaj");
    struct ne_node *(*clone)(struct ne_node *) = (void *)(ne_caution + 0xb8eafd);
    void (*construct_properties)(void *, const void *, int) = (void *)(ne_caution + 0xb8ed3d);
    if (!allocate_array) return -4;
    memcpy(ne_own_records, version->properties->records, sizeof ne_own_records);
    ne_own_records[0].value = NE_TITLE;
    /* Native binary search uses an inclusive high index; this owned table ends at 5. */
    construct_properties(&ne_own_properties, ne_own_records, 5);
    struct ne_node *added = clone(version);
    struct ne_node **children = allocate_array(6 * sizeof *children);
    if (!added || !children) return -5;
    if (added->vptr != version->vptr || added->properties != version->properties) return -6;
    added->properties = &ne_own_properties;
    memcpy(children, parent->children, 5 * sizeof *children);
    children[5] = added;
    ne_root = root; ne_parent = parent; ne_added = added;
    ne_original_children = parent->children;
    /* Only the original constructor's two fields change before native init. */
    parent->children = children;
    parent->count = 6;
    __sync_synchronize();
    ne_installed = 1;
    ne_line("NE_TREE_ADDED {\"parent\":%u,\"old_count\":5,\"new_count\":6,\"node\":%u}\n",
            (unsigned)(uintptr_t)parent, (unsigned)(uintptr_t)added);
    return 0;
}
/* Identify the single verified settings-tree initialization call.
 * views_base + 0x301921 is the Thumb return address after BLX r4 at +0x30191e.
 * Accepting another caller requires separate lifecycle/tree evidence. The
 * !ne_installed check is one-shot state, not proof of correct tree ownership
 * or synchronization if multiple UI threads could enter this function.
 */
static int ne_caller_may_insert(uintptr_t caller)
{
    /* Only the byte-verified upstream return site is accepted. The prior
     * generalized BL decoder was not valid for Thumb-tagged return addresses,
     * indirect BLX calls or PLT/vtable dispatch, and widening it would risk
     * inserting into an unrelated live settings tree. */
    return views_base && caller == views_base + 0x301921;
}

static void ne_init_wrapper(void *root, void *out, int flags)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (ne_modules_installed && !ne_installed && ne_caller_may_insert(caller)) {
        int status = ne_add_row(root);
        if (status) ne_line("NE_ERROR phase=append_row status=%d\n", status);
    }
    ne_original_init(root, out, flags);
}
static struct ne_node *ne_selected(void *view)
{
    return ((struct ne_node *(*)(void *))(views_base + 0x2f5f23))(view);
}
static int ne_is_own(struct ne_node *node)
{
    return node && node->properties == &ne_own_properties;
}
static struct ne_node *ne_find_own(struct ne_node *node, unsigned depth, unsigned *budget)
{
    if (!node || depth > 3 || !*budget) return NULL;
    --*budget;
    if (ne_is_own(node)) return node;
    if (node->count < 0 || node->count > 256 || (node->count && !node->children)) return NULL;
    for (int i = 0; i < node->count; ++i) {
        struct ne_node *found = ne_find_own(node->children[i], depth + 1, budget);
        if (found) return found;
    }
    return NULL;
}
static void ne_populate_wrapper(void *view)
{
    ne_original_populate(view);
    if (!ne_installed || ne_pointer((uintptr_t)view + 0x1b8) != (uintptr_t)ne_root) return;
    unsigned budget = 2048;
    struct ne_node *added = ne_find_own(ne_root, 0, &budget);
    if (!added || !added->parent) { ne_line("NE_ERROR phase=initialized_own_node\n"); return; }
    struct ne_node *page = added->parent;
    struct ne_node *group = page->parent;
    if (!group || group->parent != ne_root || page->count != 6 || added->physical != 6) {
        ne_line("NE_ERROR phase=initialized_path\n"); return;
    }
    int (*select_index)(void *, int) = ne_find_symbol("_ZN18CmnViewSettingNode22setItemSelectedByIndexEi");
    if (!select_index) { ne_line("NE_ERROR phase=select_symbol\n"); return; }
    select_index(ne_root, group->physical);
    select_index(group, page->physical);
    select_index(page, added->physical);
    ((void (*)(void *))(views_base + 0x2f4b5b))(view);
    int ordinal = ((int (*)(void *))(views_base + 0x2f5f65))(view);
    int group_ordinal = (int)ui_u32((unsigned char *)view + 0x168);
    if (ordinal <= 0 || group_ordinal < 0 || !ne_is_own(ne_selected(view))) {
        ne_line("NE_ERROR phase=select_readback\n"); return;
    }
    ((void (*)(void *, int, int))(views_base + 0x2ffd75))(view, ordinal, group_ordinal);
    ne_line("NE_FOCUS_ENTRY {\"group\":%d,\"page\":%d,\"index\":6}\n", group_ordinal, ordinal);
}
void native_label_text(void *label, uint32_t id)
{
    if (id == NE_TITLE && object_base) {
        __sync_add_and_fetch(&ne_title_sets, 1);
        ((void (*)(void *, const char *))(object_base + 0x11573f))(label, "New Menu");
        return;
    }
    void (*original)(void *, uint32_t) = object_base ?
        (void *)(object_base + 0x116575) : dlsym(RTLD_NEXT, "_ZN9GEN_Label7setTextEj");
    if (original) original(label, id);
    else ne_line("NE_ERROR phase=label_original\n");
}
static uintptr_t native_entry_title(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    /* Both trampolines must exist before either hook can be trusted. A missing
     * original_string_id means observe_string_id should never have been
     * installed; a missing original_string_chars means we must not rewrite the
     * title, because the only way to render "New Menu" is through that
     * trampoline. Returning silently keeps the UI alive instead of jumping to
     * address 0 mid-render. */
    if (!original_string_id || !original_string_chars) return 0;
    if (b != NE_TITLE) return original_string_id(a, b, c, d);
    __sync_add_and_fetch(&ne_title_sets, 1);
    return original_string_chars(a, (uintptr_t)"New Menu", c, d);
}
static void ne_force_frame(void *view)
{
    ((void (*)(void *, int))(views_base + 0x2f5999))(view, 1);
    /* The original direct-draw method retains its own mode gate. */
    ((void (*)(void *))(object_base + 0x8a0075))(view);
    native_entry_after_draw();
}
static int ne_bind_after_draw(void *view)
{
    if (!ne_installed || ne_pointer((uintptr_t)view + 0x1b8) != (uintptr_t)ne_root) return -1;
    if (ne_bound_view == (uintptr_t)view) return 0;
    void *widget = ((void *(*)(void *, uint32_t))(object_base + 0x1398e9))(view, 0xa056f604);
    widget = ((void *(*)(void *))(views_base + 0x861c29))(widget);
    if (!widget) return -1;
    ((void (*)(void *))(views_base + 0x2f4b5b))(view);
    ((void (*)(void *, void *))(views_base + 0x861ee7))(widget, ne_root);
    int page = ((int (*)(void *))(views_base + 0x2f5f65))(view);
    int group = (int)ui_u32((unsigned char *)view + 0x168);
    ((void (*)(void *, int, int))(views_base + 0x2ffd75))(view, page, group);
    ((void (*)(void *, int))(views_base + 0x2f5999))(view, 1);
    ne_bound_view = (uintptr_t)view;
    ne_bound_frame = headless_native_frames;
    ne_line("NE_WIDGET_BIND {\"widget\":%u,\"root\":%u,\"count\":%u,\"highlight\":%u,\"after_frame\":%llu}\n",
            (unsigned)(uintptr_t)widget, (unsigned)ne_pointer((uintptr_t)widget + 0x186ac),
            ui_u32((unsigned char *)widget + 0x1ec), ui_u32((unsigned char *)widget + 0x1f8),
            (unsigned long long)ne_bound_frame);
    return 1;
}
static void ne_main_wrapper(void *view, int *next_state, int *action)
{
    uint32_t function = ((uint32_t (*)(void *))(object_base + 0x13a695))(view);
    uint32_t packed = __sync_fetch_and_add(&ne_packed, 0);
    if (function >= 1 && function <= 6 && function == ne_pending_function &&
            ne_pending_input != ne_last_input)
        ne_required_frame = headless_native_frames + 1;
    int own = 0;
    if (ui_u32(view) == views_base + 0xbaabb0 && ui_u32((unsigned char *)view + 4) == 1)
        own = ne_is_own(ne_selected(view));
    if (function >= 1 && function <= 6 && ((packed & 0x100u) || (own && function == 5))) {
        *action = -1;
        struct menu_state state = {packed & 7u, 1};
        if (!(packed & 0x100u)) {
            menu_init(&state); ++ne_entries; packed = 0x100u | state.category;
        } else if (function == 6) {
            packed = 0; ++ne_exits;
        } else {
            static const enum menu_key keys[6] = {MENU_KEY_DOWN, MENU_KEY_UP, MENU_KEY_RIGHT,
                                                  MENU_KEY_LEFT, MENU_KEY_ENTER, MENU_KEY_BACK};
            (void)menu_key(&state, keys[function - 1]);
            packed = 0x100u | state.category;
        }
        __sync_lock_test_and_set(&ne_packed, packed);
        if (function == ne_pending_function) __sync_lock_test_and_set(&ne_last_input, ne_pending_input);
        ne_line("NE_INPUT_CONSUMED {\"sequence\":%u,\"function\":%u,\"tid\":%ld,\"overlay\":%u,\"entries\":%u,\"exits\":%u}\n",
                ne_last_input, function, syscall(224), !!(packed & 0x100u), ne_entries, ne_exits);
        ne_force_frame(view);
        return;
    }
    ne_original_main(view, next_state, action);
    if (function >= 1 && function <= 6 && function == ne_pending_function) {
        __sync_lock_test_and_set(&ne_last_input, ne_pending_input);
        ne_line("NE_INPUT_CONSUMED {\"sequence\":%u,\"function\":%u,\"tid\":%ld,\"overlay\":0,\"native_action\":%d}\n",
                ne_last_input, function, syscall(224), *action);
    }
}

static uintptr_t ne_row_wrapper(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d,
                                uintptr_t e, uintptr_t f, uintptr_t g, uintptr_t h, uintptr_t i)
{
    if (d == NE_TITLE || e == NE_TITLE || f == NE_TITLE)
        ne_line("NE_ROW {\"widget\":%u,\"ordinal\":%u,\"classification\":%u,\"arg3\":%u,\"title\":%u,\"arg5\":%u,\"arg6\":%u,\"arg7\":%u,\"arg8\":%u}\n",
                (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d, (unsigned)e,
                (unsigned)f, (unsigned)g, (unsigned)h, (unsigned)i);
    return ne_original_row(a,b,c,d,e,f,g,h,i);
}
static unsigned ne_jump(unsigned char *where, uintptr_t target)
{
    uintptr_t literal = ((uintptr_t)where + 7u) & ~(uintptr_t)3u;
    uintptr_t pc = ((uintptr_t)where + 4u) & ~(uintptr_t)3u;
    uint16_t instruction[2] = {0xf8df, (uint16_t)(0xf000u | (literal - pc))};
    uint16_t nop = 0xbf00;
    uint32_t address = (uint32_t)target;
    memcpy(where, instruction, 4);
    if (literal != (uintptr_t)where + 4u) memcpy(where + 4, &nop, 2);
    memcpy((void *)literal, &address, 4);
    return (unsigned)(literal + 4u - (uintptr_t)where);
}
static int ne_hook(uintptr_t address, const unsigned char expected[12], void *replacement, void *original)
{
    if (memcmp((void *)address, expected, 12)) return -1;
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return -2;
    memcpy(code, (void *)address, 12);
    ne_jump(code + 12, (address + 12) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 20);
    uintptr_t callable = (uintptr_t)code | 1u;
    memcpy(original, &callable, sizeof callable);
    uintptr_t page = address & ~(uintptr_t)4095;
    unsigned bytes = (address & 3u) ? 10u : 8u;
    size_t protection_span = ((address + bytes + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, protection_span, PROT_READ | PROT_WRITE | PROT_EXEC)) return -3;
    unsigned written = ne_jump((void *)address, (uintptr_t)replacement);
    __builtin___clear_cache((char *)address, (char *)address + written);
    if (mprotect((void *)page, protection_span, PROT_READ | PROT_EXEC)) return -4;
    return 0;
}
static void native_entry_modules(void *handle)
{
    if (ne_modules_installed || !object_base || !views_base || draw_count) return;
    ne_view_handle = handle;
    void *ctor = ne_find_symbol("_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
    Dl_info info;
    if (!ctor || !dladdr(ctor, &info) || !info.dli_fbase) return;
    ne_caution = (uintptr_t)info.dli_fbase;
    if ((uintptr_t)ctor - ne_caution != 0xb8eb21) return;
    if (memcmp((void *)(ne_caution + 0xb8dffa), ne_pin_init, 12) ||
        memcmp((void *)(views_base + 0x3014f0), ne_pin_populate, 12) ||
        memcmp((void *)(views_base + 0x2fccc0), ne_pin_main, 12) ||
        memcmp((void *)(views_base + 0x862130), ne_pin_row, 12)) {
        ne_line("NE_ERROR phase=native_prologue\n"); return;
    }
    if (ne_hook(ne_caution + 0xb8dffa, ne_pin_init, ne_init_wrapper, &ne_original_init) ||
        ne_hook(views_base + 0x3014f0, ne_pin_populate, ne_populate_wrapper, &ne_original_populate) ||
        ne_hook(views_base + 0x2fccc0, ne_pin_main, ne_main_wrapper, &ne_original_main) ||
        ne_hook(views_base + 0x862130, ne_pin_row, ne_row_wrapper, &ne_original_row)) {
        ne_line("NE_ERROR phase=hook_install\n"); return;
    }
    ne_modules_installed = 1;
    ne_line("NE_MODULES {\"pid\":%d,\"hooks\":4}\n", getpid());
}
static int native_entry_present(void *pixels, size_t length, uint32_t physical, uintptr_t caller)
{
    uint32_t packed = __sync_fetch_and_add(&ne_packed, 0);
    if (!(packed & 0x100u)) return 0;
    if (length != 640u * 480u * 2u || caller != object_base + 0x777935) {
        ne_line("NE_ERROR phase=present_context caller=%#lx span=%u\n", (unsigned long)caller, (unsigned)length);
        return -1;
    }
    struct menu_state state = {packed & 7u, 1};
    if (menu_render(&state, pixels, 640u * 480u)) return -1;
    __sync_synchronize();
    pthread_mutex_lock(&ne_present_mutex);
    ne_overlay_physical = physical;
    ne_overlay_fingerprint = headless_fingerprint(pixels, length);
    ne_overlay_packed = packed;
    pthread_mutex_unlock(&ne_present_mutex);
    return 0;
}
static uint64_t ne_overlay_hash(uint32_t physical)
{
    uint64_t value = 0;
    pthread_mutex_lock(&ne_present_mutex);
    uint32_t packed = __sync_fetch_and_add(&ne_packed, 0);
    if ((packed & 0x100u) && packed == ne_overlay_packed && physical == ne_overlay_physical)
        value = ne_overlay_fingerprint;
    pthread_mutex_unlock(&ne_present_mutex);
    return value;
}
static int ne_post_function(uint32_t sequence, uint32_t function)
{
    if (function < 1 || function > 6 || !sequence || !ne_modules_installed) return -1;
    uintptr_t manager = ne_pointer(object_base + 0x149221c);
    if (!manager || ui_u32((void *)manager) != 43) return -2;
    uintptr_t events = ne_pointer(manager + 0x10);
    if (!events) return -3;
    void *(*allocate)(size_t) = ne_find_symbol("_Znwj");
    if (!allocate) return -4;
    void *event = allocate(16);
    if (!event) return -5;
    uint32_t id = ((uint32_t (*)(uint32_t, uint32_t, uint32_t))(object_base + 0x117789))(0x13000000u, 0xbau, function);
    ne_pending_function = function;
    __sync_lock_test_and_set(&ne_pending_input, sequence);
    ((void (*)(void *, uint32_t, unsigned, unsigned))(object_base + 0x8a01e9))(event, id, 4, 1);
    /* EventManager retains Event*: the original consumer owns destruction and delete. */
    ((int (*)(void *, void *, int))(object_base + 0x8a1795))((void *)events, event, 1);
    ne_line("NE_EVENT_QUEUED {\"sequence\":%u,\"function\":%u,\"event\":%u,\"model\":\"native-function-event\"}\n",
            sequence, function, id);
    return 0;
}
static void native_entry_after_draw(void)
{
    if (!ne_modules_installed || headless_native_frames < ne_required_frame) return;
    struct ne_observation state = {0};
    state.pid = (uint32_t)getpid(); state.ui_tid = (uint32_t)ui_tid;
    state.node_installed = ne_installed; state.title_sets = __sync_fetch_and_add(&ne_title_sets, 0);
    uint32_t packed = __sync_fetch_and_add(&ne_packed, 0);
    state.overlay = !!(packed & 0x100u); state.category = packed & 7u;
    state.entries = ne_entries; state.exits = ne_exits; state.last_input = ne_last_input;
    uintptr_t manager = ne_pointer(object_base + 0x149221c);
    if (manager && ui_u32((void *)manager) == 43) {
        uint32_t *slots = (void *)ne_pointer(manager + 0x20);
        if (slots) for (unsigned i = 0; i < 43; ++i) if (slots[i * 4 + 2] == 0xba && slots[i * 4 + 1]) {
            uintptr_t view = slots[i * 4 + 1];
            if (ui_u32((void *)view) != views_base + 0xbaabb0) continue;
            state.view = (uint32_t)view;
            state.view_state = ui_u32((void *)(view + 4));
            state.submode = ui_u32((void *)(view + 0x164));
            if (state.view_state == 1) {
                int rebound = ne_bind_after_draw((void *)view);
                if (rebound < 0) return;
                if (rebound > 0) { ne_force_frame((void *)view); return; }
                if (headless_native_frames <= ne_bound_frame) return;
                struct ne_node *selected = ne_selected((void *)view);
                if (selected && selected->parent) {
                    state.parent = (uint32_t)(uintptr_t)selected->parent;
                    state.count = (uint32_t)selected->parent->count;
                    state.selected_index = (uint32_t)selected->parent->selected;
                    state.selected_own = (uint32_t)ne_is_own(selected);
                }
            }
            break;
        }
    }
    ne_transport_ui(&state, headless_native_frames);
    ne_transport_start();
}

#include "transport_nop.inc"

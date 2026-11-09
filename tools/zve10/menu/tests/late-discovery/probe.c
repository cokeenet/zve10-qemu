/* Late-load discovery only: no constructor, interposer, patch, thread or file IO.
 * caller explicitly invokes nm_discovery_probe after dlopen.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct nm_discovery_result {
    uint32_t abi_version, result_bytes;
    uintptr_t object_base, views_base, caution_base;
    uintptr_t view_handle, caution_handle;
    uint32_t found_mask, anchor_mask;
};
static struct nm_discovery_result state;

static int discover(struct dl_phdr_info *info, size_t size, void *unused)
{
    (void)size; (void)unused;
    const char *path = info->dlpi_name;
    if (!path || !*path) return 0;
    if (strstr(path, "libObj.so")) {
        state.object_base = info->dlpi_addr;
        state.found_mask |= 1u;
    }
    if (strstr(path, "viewUnified2.so")) {
        state.views_base = info->dlpi_addr;
        state.view_handle = (uintptr_t)dlopen(path, RTLD_NOLOAD | RTLD_LAZY);
        if (state.view_handle) state.found_mask |= 2u;
    }
    if (strstr(path, "CautionConfig.so")) {
        state.caution_base = info->dlpi_addr;
        state.caution_handle = (uintptr_t)dlopen(path, RTLD_NOLOAD | RTLD_LAZY);
        if (state.caution_handle) state.found_mask |= 4u;
    }
    return 0;
}

__attribute__((visibility("default")))
int nm_discovery_probe(struct nm_discovery_result *out, size_t bytes)
{
    if (!out || bytes != sizeof *out) return -1;
    /* Do not accumulate RTLD_NOLOAD references over repeated explicit probes. */
    if (state.view_handle) dlclose((void *)state.view_handle);
    if (state.caution_handle) dlclose((void *)state.caution_handle);
    memset(&state, 0, sizeof state);
    state.abi_version = 1;
    state.result_bytes = sizeof state;
    dl_iterate_phdr(discover, NULL);
    if (state.view_handle) {
        void *symbol = dlsym((void *)state.view_handle, "ViewRootSelectorToInstance");
        Dl_info info;
        if (symbol && dladdr(symbol, &info) && (uintptr_t)info.dli_fbase == state.views_base)
            state.anchor_mask |= 2u;
    }
    if (state.caution_handle) {
        void *symbol = dlsym((void *)state.caution_handle,
            "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
        Dl_info info;
        if (symbol && dladdr(symbol, &info) && (uintptr_t)info.dli_fbase == state.caution_base
            && (uintptr_t)symbol - state.caution_base == 0xb8eb21)
            state.anchor_mask |= 4u;
    }
    *out = state;
    return state.found_mask == 7u ? 0 : 1;
}

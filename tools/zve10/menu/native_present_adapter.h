/* Native GPU path: preserve submit/wait, overlay only after completion.
 * Included after native_entry.inc and ui_present_span(). No QEMU GPU emulation.
 */
#ifndef NM_NATIVE_PRESENT_ADAPTER_H
#define NM_NATIVE_PRESENT_ADAPTER_H

static entry_fn nm_original_present, nm_original_engine_wait;
struct nm_pending_present {
    uint32_t physical;
    size_t span;
    uintptr_t caller;
};
/* Startup-preload only. Initial-exec avoids introducing the cross-toolchain's
 * ld-linux-armhf.so.3 dependency through __tls_get_addr. Not for late dlopen. */
static __thread struct nm_pending_present nm_pending_present
    __attribute__((tls_model("initial-exec")));
static void nm_overlay_completed(const struct nm_pending_present *pending);

static void nm_capture_present(uintptr_t params, uintptr_t caller)
{
    size_t span = params ? ui_present_span((const void *)params) : 0;
    struct nm_pending_present pending = {0};
    /* Accept only the captured full-frame submit from the known render path. */
    if (span && caller == object_base + 0x777935) {
        const uint32_t *p = (const uint32_t *)params;
        pending.physical = p[1];
        pending.span = span;
        pending.caller = caller;
    }
    nm_pending_present = pending;
}

static uintptr_t nm_observe_present(uintptr_t params, uintptr_t b,
                                    uintptr_t c, uintptr_t d)
{
    nm_capture_present(params, (uintptr_t)__builtin_return_address(0));
    return nm_original_present(params, b, c, d);
}

static uintptr_t nm_observe_engine_wait(uintptr_t engine, uintptr_t b,
                                        uintptr_t c, uintptr_t d)
{
    /* The original function owns hardware completion and error handling. */
    uintptr_t status = nm_original_engine_wait(engine, b, c, d);
    if (engine != 2 || !(status & UINT32_C(0x40000000))) return status;
    struct nm_pending_present pending = nm_pending_present;
    nm_pending_present = (struct nm_pending_present){0};
    if (!pending.span) return status;
    if (__sync_fetch_and_add(&ne_packed, 0) & 0x100u)
        nm_overlay_completed(&pending);
    return status;
}

#endif

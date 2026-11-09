/* Install only fixed, decoded position-independent prologues.
 * Wait's third instruction is followed by a relative branch at +10: copy 8,
 * not 12. The 12-byte pins still validate target identity before mutation.
 */
static int nm_install_present_pair(void)
{
    static const unsigned char submit_pin[12] = {
        0x2d,0xe9,0xf0,0x41,0x84,0xb0,0x05,0x46,0x04,0xaf,0x03,0x6b
    };
    static const unsigned char wait_pin[12] = {
        0xf0,0xb5,0x83,0xb0,0x44,0x1e,0x00,0xaf,0x07,0x2c,0x09,0xd8
    };
    struct nm_patch {
        uintptr_t address;
        const unsigned char *pin;
        unsigned displaced;
        entry_fn replacement;
        entry_fn *original;
    } patches[2] = {
        {object_base + 0x774ef0, submit_pin, 12, nm_observe_present, &nm_original_present},
        {object_base + 0x775120, wait_pin, 8, nm_observe_engine_wait, &nm_original_engine_wait}
    };
    for (unsigned i = 0; i < 2; ++i)
        if (memcmp((void *)patches[i].address, patches[i].pin, 12)) return -1;
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return -2;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *slot = code + 32 * i;
        memcpy(slot, (void *)patches[i].address, patches[i].displaced);
        unsigned length = ne_jump(slot + patches[i].displaced,
                                  (patches[i].address + patches[i].displaced) | 1u);
        __builtin___clear_cache((char *)slot,
                               (char *)slot + patches[i].displaced + length);
    }
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC)) {
        munmap(code, 4096); return -3;
    }
    unsigned writable = 0;
    for (; writable < 2; ++writable) {
        uintptr_t page = patches[writable].address & ~(uintptr_t)4095;
        if (mprotect((void *)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            for (unsigned j = 0; j < writable; ++j)
                mprotect((void *)(patches[j].address & ~(uintptr_t)4095), 4096,
                         PROT_READ | PROT_EXEC);
            munmap(code, 4096); return -4;
        }
    }
    /* Publish both trampolines before either branch becomes visible. Startup
     * only: concurrent live patching is not supported by this installer. */
    for (unsigned i = 0; i < 2; ++i)
        *patches[i].original = (entry_fn)((uintptr_t)(code + 32 * i) | 1u);
    __sync_synchronize();
    for (unsigned i = 0; i < 2; ++i) {
        unsigned length = ne_jump((void *)patches[i].address,
                                  (uintptr_t)patches[i].replacement);
        __builtin___clear_cache((char *)patches[i].address,
                               (char *)patches[i].address + length);
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (mprotect((void *)(patches[i].address & ~(uintptr_t)4095), 4096,
                     PROT_READ | PROT_EXEC)) return -5;
    }
    return 0;
}

static void nm_overlay_completed(const struct nm_pending_present *pending)
{
    /* ui_present_span already checked the exact format and physical range. */
    size_t delta = pending->physical & 4095u;
    size_t mapped = (pending->span + delta + 4095u) & ~(size_t)4095;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { nm_log("NM present map open failed errno=%d\n", errno); return; }
    void *base = mmap(NULL, mapped, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                      pending->physical & ~UINT32_C(4095));
    close(fd);
    if (base == MAP_FAILED) {
        nm_log("NM present mmap failed errno=%d\n", errno); return;
    }
    int result = native_entry_present((unsigned char *)base + delta,
                                      pending->span, pending->physical,
                                      pending->caller);
    __sync_synchronize();
    if (result) nm_log("NM present overlay failed status=%d\n", result);
    if (munmap(base, mapped)) nm_log("NM present munmap failed errno=%d\n", errno);
}

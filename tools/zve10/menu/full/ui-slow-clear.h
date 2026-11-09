/* Native slow clear B+7c9ba0: see evidence/ui-native-commands and ui-v7-slow-clear-state.log. */
#ifndef UI_SLOW_CLEAR_H
#define UI_SLOW_CLEAR_H

static uintptr_t software_slow_clear(uintptr_t thread,uintptr_t mask,uintptr_t c,uintptr_t d)
{
    (void)c;(void)d;
    /* shortcut: captured RGBA4444 zero clear; implement other colors/attachments from live evidence. */
    if (mask!=0x4500 && mask!=0x500) gemini_cpu_unsupported("GPE.clear.slow.mask",mask);
    uintptr_t gl=((uintptr_t (*)(uintptr_t))(object_base+0x7bbb41))(thread);
    uintptr_t outer=ui_u32((void *)(object_base+0x145cbc8));
    if (!gemini_cpu_pointer(outer,0x14) || ui_u32((void *)outer)!=object_base+0x13bf558)
        gemini_cpu_unsupported("GPE.clear.slow.outer",outer);
    uintptr_t s=ui_u32((void *)(outer+0x10)), target[6], after[6];
    if (!gemini_cpu_pointer(s,0x174) || ui_u32((void *)s)!=object_base+0x13bf2e0 ||
        !gemini_cpu_current(s) || gemini_cpu_target(s,target) || gl!=target[3] ||
        !*(unsigned char *)(gl+0x1875) || *(unsigned char *)(gl+0x1876) ||
        ui_u32((void *)(gl+0x1878))!=target[5] || ui_u32((void *)(gl+0x1880))!=640 ||
        ui_u32((void *)(gl+0x1884))!=480 || ui_u32((void *)(gl+0x1888))!=4 ||
        ui_u32((void *)(gl+0x698))!=0x01010101 || ui_u32((void *)(gl+0x69c)) ||
        ui_u32((void *)(gl+0x6a0))!=UINT32_MAX || ui_u32((void *)(gl+0x6a8)) ||
        ui_u32((void *)(gl+0x6ac)) || ui_u32((void *)(gl+0x6b0)) || ui_u32((void *)(gl+0x6b4)))
        gemini_cpu_unsupported("GPE.clear.slow.state",gl);
    uint32_t rect[4]={0,0,640,480};
    if (*(unsigned char *)(gl+0x608))
        for (unsigned i=0;i<4;++i) rect[i]=ui_u32((void *)(gl+0x60c+i*4));
    if (rect[0]>640 || rect[1]>480 || rect[2]>640-rect[0] || rect[3]>480-rect[1])
        gemini_cpu_unsupported("GPE.clear.slow.scissor",rect[0]);
    ((void (*)(void))(object_base+0x7c0d31))();
    if (!gemini_cpu_current(s) || gemini_cpu_target(s,after) || memcmp(target,after,sizeof target))
        gemini_cpu_unsupported("GPE.clear.slow.changed_target",gl);
    if (mask&0x4000) {
        struct surface_mapping mapping;
        if (map_surface((uint32_t)target[5],UI_PRESENT_BYTES,&mapping))
            gemini_cpu_unsupported("GPE.clear.slow.map",target[5]);
        int result=ui_slow_clear_pixels(mapping.pixels,UI_PRESENT_BYTES,rect);
        __sync_synchronize();
        if (munmap(mapping.base,mapping.length) || result)
            gemini_cpu_unsupported("GPE.clear.slow.copy",(uintptr_t)result);
        record("GPE.clear.slow.color_pixels",rect[2]*rect[3]);
    }
    /* No depth/stencil attachment exists in this guarded path; retain native state-cache invalidation. */
    ((void (*)(uintptr_t))(object_base+0x791e91))(gl);
    record("GPE.clear.slow.cpu_completed",mask);
    return 0;
}
#endif

/* Include after ui-raster.h, ui-gemini-raster.h, map_surface(), record(),
 * object_base and ui-draw-probe.h. At cold install, after trampoline creation:
 * gemini_original_solid=software_gemini_solid; gemini_original_rect=software_gemini_rect;
 * gemini_original_quad=software_gemini_quad; gemini_original_line=software_gemini_line;
 * The probes then log and dispatch here; never call the original geometry too.
 */
#ifndef UI_GEMINI_BACKEND_H
#define UI_GEMINI_BACKEND_H

#ifndef GEMINI_CPU_NATIVE
#define GEMINI_CPU_NATIVE(offset) (object_base + (offset))
#endif

static __attribute__((noreturn)) void gemini_cpu_unsupported(const char *detail, uintptr_t value)
{
    record("GEMINI.cpu.UNSUPPORTED", value);
    record(detail, value);
    _exit(71);
}

static int gemini_cpu_pointer(uintptr_t p, size_t span)
{
    return p >= 4096 && span <= UINT32_MAX && p <= UINT32_MAX - span;
}

static int gemini_cpu_empty(uintptr_t r)
{
    return gemini_cpu_pointer(r,0x64) && !gemini_probe_read(r,0x4c) &&
           !gemini_probe_read(r,0x58) && !ui_u16((void *)(r+0x62));
}

static int gemini_cpu_current(uintptr_t s)
{
    uintptr_t thread = ((uintptr_t (*)(void))GEMINI_CPU_NATIVE(0x7bbb31))();
    if (!thread) return 0;
    uintptr_t context = ((uintptr_t (*)(uintptr_t))GEMINI_CPU_NATIVE(0x7bbbe1))(thread);
    uintptr_t surface = ((uintptr_t (*)(uintptr_t))GEMINI_CPU_NATIVE(0x7bbc21))(thread);
    return context && surface && context == gemini_probe_read(s,0x170) &&
           surface == gemini_probe_read(s,0x16c);
}

/* Snapshot the exact captured default target; no address resolver/cache writes. */
static int gemini_cpu_target(uintptr_t s, uintptr_t out[6])
{
    if (gemini_probe_read(s,0x148)) return -5; /* Offscreen target not established live. */
    uintptr_t context = gemini_probe_read(s,0x170), surface = gemini_probe_read(s,0x16c);
    if (!gemini_cpu_pointer(context,0x30) || !gemini_cpu_pointer(surface,0x9c) ||
        gemini_probe_read(context,0x10) != 2) return -5;
    uintptr_t holder = gemini_probe_read(context,0x2c);
    if (!gemini_cpu_pointer(holder,4)) return -5;
    uintptr_t gl = gemini_probe_read(holder,0);
    if (!gemini_cpu_pointer(gl,0x6d4) || gemini_probe_read(gl,0x6d0) ||
        gemini_probe_read(surface,8) != 1 || gemini_probe_read(surface,0x1c) != 3 ||
        gemini_probe_read(surface,0x14) != 480 || gemini_probe_read(surface,0x18) != 640 ||
        gemini_probe_read(surface,0x30) != 480 || gemini_probe_read(surface,0x34) != 640 ||
        gemini_probe_read(surface,0x8c) != 640 || gemini_probe_read(surface,0x90) != 480) return -5;
    uintptr_t ma = gemini_probe_read(surface,0x78);
    if (!gemini_cpu_pointer(ma,0x1c)) return -5;
    uint32_t physical = gemini_probe_read(ma,8);
    /* shortcut: only the live 640x480 RGBA4444 MA flags/geometry; verify a new
     * capture before accepting another allocation class or storage shape. */
    if (gemini_probe_read(ma,0xc) != 640u*480*2 || gemini_probe_read(ma,0x10) != 1280 ||
        gemini_probe_read(ma,0x14) != 0x8001 || gemini_probe_read(ma,0x18) ||
        physical < 0x20000000u || physical > 0x28000000u - 640u*480*2 || (physical & 1)) return -5;
    out[0]=context; out[1]=surface; out[2]=holder; out[3]=gl; out[4]=ma; out[5]=physical;
    return 0;
}

struct gemini_cpu_source {
    uintptr_t texture, bitmap, meta;
    uint32_t texture_fields[7], bitmap_fields[3], metadata[13];
};

static int gemini_cpu_source_snapshot(uintptr_t texture, struct gemini_cpu_source *out)
{
    if (!gemini_cpu_pointer(texture,0x4c) ||
        gemini_probe_read(texture,0) != object_base+0x13bf150) return -5;
    out->texture = texture;
    out->texture_fields[0] = gemini_probe_read(texture,0x14);
    out->texture_fields[1] = gemini_probe_read(texture,0x1c);
    out->texture_fields[2] = gemini_probe_read(texture,0x40);
    /* Parameter values only, excluding the interleaved dirty bytes. */
    out->texture_fields[3] = gemini_probe_read(texture,0x34) & 0x00ff00ff;
    out->texture_fields[4] = gemini_probe_read(texture,0x38) & 0x00ff00ff;
    out->texture_fields[5] = gemini_probe_read(texture,0x3c) & 0xff;
    out->texture_fields[6] = gemini_probe_read(texture,0x44) & 1;
    out->bitmap = gemini_probe_read(texture,0x48);
    if (!gemini_cpu_pointer(out->bitmap,12) ||
        gemini_probe_read(out->bitmap,0) != object_base+0x13ba5a0) return -5;
    memcpy(out->bitmap_fields,(void *)out->bitmap,sizeof out->bitmap_fields);
    out->meta = out->bitmap_fields[1];
    if (!gemini_cpu_pointer(out->meta,sizeof out->metadata)) return -5;
    memcpy(out->metadata,(void *)out->meta,sizeof out->metadata);
    return 0;
}

static int gemini_cpu_source_same(const struct gemini_cpu_source *before)
{
    struct gemini_cpu_source after = {0};
    return !gemini_cpu_source_snapshot(before->texture,&after) &&
           before->bitmap == after.bitmap && before->meta == after.meta &&
           !memcmp(before->texture_fields,after.texture_fields,sizeof after.texture_fields) &&
           !memcmp(before->bitmap_fields,after.bitmap_fields,sizeof after.bitmap_fields) &&
           !memcmp(before->metadata,after.metadata,sizeof after.metadata);
}

static int gemini_cpu_integral(float n)
{
    return isfinite(n) && n >= -65536 && n <= 65536 && n == (int)n;
}

static uintptr_t gemini_cpu_draw(unsigned kind, uintptr_t r, uintptr_t s, uintptr_t m,
                                 uintptr_t rgba, uintptr_t first, uintptr_t second)
{
    if (!object_base || !gemini_cpu_pointer(s,0x174) ||
        gemini_probe_read(s,0) != object_base+0x13bf2e0 || !gemini_cpu_empty(r))
        gemini_cpu_unsupported("GEMINI.cpu.context_or_batch",s);
    if (!gemini_cpu_pointer(m,0x38) || !gemini_cpu_pointer(rgba,4) ||
        !gemini_cpu_pointer(first,8) || (kind && !gemini_cpu_pointer(second,kind == 2 ? 32 : 8)))
        gemini_cpu_unsupported("GEMINI.cpu.arguments",first);
    /* Only six floats are proven readable at the original affine pointer. */
    float matrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static const unsigned fields[] = {0,1,4,5,12,13};
    for (unsigned i=0;i<6;++i) memcpy(matrix+fields[i],(void *)(m+fields[i]*4),4);
    unsigned char color[4]; memcpy(color,(void *)rgba,4);
    struct ui_gemini_rect sr = {0}, dr = {0};
    float points[8] = {0};
    if (!kind) memcpy(&dr,(void *)first,8);
    else {
        memcpy(&sr,(void *)first,8);
        if (kind == 2) memcpy(points,(void *)second,sizeof points);
        else memcpy(&dr,(void *)second,8);
    }
    struct ui_gemini_state state = {0};
    memcpy(&state.viewport,(void *)(s+0xa8),8);
    memcpy(&state.scissor,(void *)(s+0x60),8);
    state.blend = gemini_probe_read(s,0x30);
    state.blend_enabled = gemini_probe_read(s,0x2c);
    state.scissor_enabled = gemini_probe_read(s,0x34);
    state.rgb_write = gemini_probe_read(s,0x58);
    state.alpha_write = gemini_probe_read(s,0x5c);
    uintptr_t target[6], after[6];
    if (!gemini_cpu_current(s) || gemini_cpu_target(s,target))
        gemini_cpu_unsupported("GEMINI.cpu.current_or_default_target",s);
    struct gemini_cpu_source source = {0};
    struct ui_gemini_surface src = {0};
    unsigned mode = UI_GEMINI_TEXTURE;
    if (kind) {
        if (gemini_cpu_source_snapshot(gemini_probe_read(s,0xb8),&source))
            gemini_cpu_unsupported("GEMINI.cpu.source_identity",gemini_probe_read(s,0xb8));
        const unsigned char *meta = (void *)source.metadata;
        unsigned format = ui_u32(meta+0x1c), sorting = ui_u32(meta+0x2c);
        if (sorting > 1) gemini_cpu_unsupported("GEMINI.cpu.source_sorting",sorting);
        if (ui_u32(meta+0x30)) gemini_cpu_unsupported("GEMINI.cpu.source_start",ui_u32(meta+0x30));
        if (ui_u32(meta+4) || ui_u32(meta+0x14))
            gemini_cpu_unsupported("GEMINI.cpu.source_origin",source.meta);
        unsigned aw=ui_u16(meta+0x18), ah=ui_u16(meta+0x1a);
        src.width=ui_u16(meta+8); src.height=ui_u16(meta+10);
        if (format == 1 && source.texture_fields[2] == 7 &&
            (source.texture_fields[1] == 0x302 || source.texture_fields[1] == 7)) {
            if (sorting && (source.texture_fields[1] != 0x302 || !source.texture_fields[6]))
                gemini_cpu_unsupported("GEMINI.cpu.source_sorting",sorting);
            src.format=0; src.stride=aw*2; mode=UI_GEMINI_MULTIPLY;
            src.layout=sorting ? UI_GEMINI_MORTON_TOP_DOWN : UI_GEMINI_LINEAR;
        } else if (format == 4 && (source.texture_fields[1] == 8 || source.texture_fields[1] == 0x304) && source.texture_fields[2] == 8) {
            if (sorting && (source.texture_fields[1]!=0x304 || !source.texture_fields[6]))
                gemini_cpu_unsupported("GEMINI.cpu.source_sorting",sorting);
            src.format=3; src.stride=aw; mode=UI_GEMINI_ALPHA_MASK;
            src.layout=sorting ? UI_GEMINI_MORTON_TOP_DOWN : UI_GEMINI_LINEAR;
        } else {
            gemini_probe_texture("GEMINI.unsupported.T",source.texture,"GEMINI.unsupported.bmp");
            gemini_cpu_unsupported("GEMINI.cpu.source_format",format);
        }
        src.capacity=(size_t)src.stride*ah;
        src.pixels=(void *)(uintptr_t)source.bitmap_fields[2];
        uint32_t physical=ui_u32(meta+0x24);
        if (!aw || !ah || aw > 8192 || ah > 8192 || src.width > aw || src.height > ah ||
            source.texture_fields[0] != (aw | ah << 16) || src.capacity > 64u*1024*1024 ||
            !gemini_cpu_pointer((uintptr_t)src.pixels,src.capacity) ||
            physical < 0x20000000u || physical > 0x28000000u-src.capacity ||
            ((uint64_t)physical+src.capacity > target[5] && target[5]+640u*480*2 > physical))
            gemini_cpu_unsupported("GEMINI.cpu.source_span_or_alias",physical);
        /* GL_LINEAR and GL_NEAREST agree on the verified 1:1 texel-center grid. */
        if ((source.texture_fields[3] & 255) > 1 || (source.texture_fields[3] >> 16) > 1)
            gemini_cpu_unsupported("GEMINI.cpu.source_filter",source.texture_fields[3]);
        if (matrix[0] != 1 || matrix[5] != 1 || matrix[1] != 0 || matrix[4] != 0 ||
            !gemini_cpu_integral(matrix[12]) || !gemini_cpu_integral(matrix[13]))
            gemini_cpu_unsupported("GEMINI.cpu.source_filter_transform",m);
        if (kind == 2) {
            for (unsigned i=0;i<8;++i) if (!gemini_cpu_integral(points[i]))
                gemini_cpu_unsupported("GEMINI.cpu.source_filter_quad",second);
            if ((double)points[6]-points[0] != sr.w || (double)points[3]-points[1] != sr.h)
                gemini_cpu_unsupported("GEMINI.cpu.source_filter_scale",second);
        } else if (dr.w != sr.w || dr.h != sr.h)
            gemini_cpu_unsupported("GEMINI.cpu.source_filter_scale",second);
    }
    ((void (*)(void))GEMINI_CPU_NATIVE(0x7c0d31))(); /* Original synchronous GL/RM finish. */
    if (!gemini_cpu_current(s) || !gemini_cpu_empty(r) || gemini_cpu_target(s,after) ||
        memcmp(target,after,sizeof target) || (kind && !gemini_cpu_source_same(&source)))
        gemini_cpu_unsupported("GEMINI.cpu.changed_during_finish",s);
    int leased=0, mapped=0, result=-5;
    const char *error="GEMINI.cpu.raster";
    struct surface_mapping mapping;
    if (kind) {
        uint32_t bits=0;
        if (((int (*)(uintptr_t,uint32_t,uint32_t *))GEMINI_CPU_NATIVE(0x69b901))(source.bitmap,0,&bits)) {
            error="GEMINI.cpu.read_lock"; goto done;
        }
        leased=1;
        if (bits != source.bitmap_fields[2] || !gemini_cpu_source_same(&source)) {
            error="GEMINI.cpu.changed_during_lock"; goto done;
        }
        src.pixels=(void *)(uintptr_t)bits;
    }
    if (map_surface((uint32_t)target[5],640u*480*2,&mapping)) {
        error="GEMINI.cpu.map_target"; goto done;
    }
    mapped=1;
    if (gemini_cpu_target(s,after) || memcmp(target,after,sizeof target) ||
        (kind && !gemini_cpu_source_same(&source))) {
        error="GEMINI.cpu.changed_during_map"; goto done;
    }
    struct ui_gemini_surface dst = {mapping.pixels,640u*480*2,640,480,1280,0,UI_GEMINI_MORTON_BOTTOM_UP};
    result = kind == 2 ? ui_gemini_quad(&dst,&src,&state,matrix,color,&sr,points,mode) :
                        ui_gemini_rect(&dst,kind ? &src : NULL,&state,matrix,color,
                                       kind ? &sr : NULL,&dr,mode);
    if (kind && !gemini_cpu_source_same(&source)) { result=-5; error="GEMINI.cpu.changed_during_copy"; }
    if (gemini_cpu_target(s,after) || memcmp(target,after,sizeof target) || !gemini_cpu_empty(r)) {
        result=-5; error="GEMINI.cpu.target_changed_during_copy";
    }
    __sync_synchronize();
done:
    if (leased) {
        ((void (*)(uintptr_t))GEMINI_CPU_NATIVE(0x69b29d))(source.bitmap);
        if (!gemini_cpu_source_same(&source) && !result) {
            result=-5; error="GEMINI.cpu.changed_during_unlock";
        }
    }
    if (mapped && munmap(mapping.base,mapping.length)) { result=-5; error="GEMINI.cpu.unmap_target"; }
    __sync_synchronize();
    if (result) gemini_cpu_unsupported(error,(uintptr_t)result);
    if (kind && src.format==3) {
        int found=0;
        for (unsigned i=0;i<native_glyph_images;++i) found|=native_glyph_bitmaps[i]==source.bitmap;
        if (!found && native_glyph_images<64) {
            native_glyph_bitmaps[native_glyph_images++]=source.bitmap;
            record("GEMINI.cpu.glyph_images",native_glyph_images);
        }
        if (++native_glyph_draws<=24) record("GEMINI.cpu.glyph_draw",native_glyph_draws);
    }
    return 1;
}

static uintptr_t software_gemini_solid(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t dst)
{
    return gemini_cpu_draw(0,r,s,m,c,dst,0);
}
static uintptr_t software_gemini_rect(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t src, uintptr_t dst)
{
    return gemini_cpu_draw(1,r,s,m,c,src,dst);
}
static uintptr_t software_gemini_quad(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t src, uintptr_t points)
{
    return gemini_cpu_draw(2,r,s,m,c,src,points);
}
static uintptr_t software_gemini_line(uintptr_t r, uintptr_t s, uintptr_t m, uintptr_t c, uintptr_t first, uintptr_t second)
{
    (void)r; (void)m; (void)c; (void)first; (void)second;
    gemini_cpu_unsupported("GEMINI.cpu.line",s);
}
#endif

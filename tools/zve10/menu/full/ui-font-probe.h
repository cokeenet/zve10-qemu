/* Native text ABIs: evidence/ui-font/REPORT.md. Forward without changing text state. */
#ifndef UI_FONT_PROBE_H
#define UI_FONT_PROBE_H
static entry_fn original_sprite_gate, original_face_select, original_string_id, original_string_chars;
static entry_fn original_text_draw, original_child_draw, original_image_realize, original_block_convert, original_image_result;
static entry_fn original_text_measure, original_image_allocate;
static unsigned font_measure_calls, font_allocate_calls;
static entry_fn original_slow_clear;
static unsigned slow_clear_calls;
static uintptr_t observe_slow_clear(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&slow_clear_calls,1);
    record("GPE.slow_clear.enter",n);record("GPE.slow_clear.mask",b);
    uintptr_t gl=((uintptr_t (*)(uintptr_t))(object_base+0x7bbb41))(a);
    if (gl) {
        const unsigned offsets[]={0x608,0x60c,0x610,0x614,0x618,0x698,0x69c,0x6a0,0x6a8,0x6ac,0x6b0,0x6b4,0x6d0,0x1874,0x1878,0x1880,0x1884,0x1888,0x188c};
        for (unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i) {
            char key[48];snprintf(key,sizeof key,"GPE.slow_clear.gl+%04x",offsets[i]);
            record(key,ui_u32((void *)(gl+offsets[i])));
        }
    }
    uintptr_t result=original_slow_clear(a,b,c,d);
    record("GPE.slow_clear.return",n);
    return result;
}
static entry_fn original_text_command_execute, original_text_command_post;
static unsigned font_execute_calls, font_post_calls;
static uintptr_t observe_text_command_execute(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_execute_calls,1);
    if (n<=4 || n>116) { record("FONT.command.execute",a);record("FONT.command.execute.hash",ui_u32((void *)(a+0x18))); }
    uintptr_t result=original_text_command_execute(a,b,c,d);
    if (n<=4 || n>116) record("FONT.command.execute.result",result);
    return result;
}
static uintptr_t observe_text_command_post(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_post_calls,1);
    if (n<=4 || n>116) { record("FONT.command.post",a);record("FONT.command.post.result",b); }
    return original_text_command_post(a,b,c,d);
}
static gemini_probe_fn6 original_text_image_request;
static unsigned font_request_calls;
static uintptr_t observe_text_image_request(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t user,uintptr_t output)
{
    unsigned n=__sync_add_and_fetch(&font_request_calls,1);
    uintptr_t result=original_text_image_request(a,b,c,d,user,output);
    if (n<=4 || (n>116 && n<=160) || !result) {
        record("FONT.request.ordinal",n);
        record("FONT.request.index",c);record("FONT.request.result",result);
        record("FONT.request.record",b);record("FONT.request.callback",d);
        record("FONT.request.user",user);record("FONT.request.output",output);
        if (output) {
            uintptr_t hash=ui_u32((void *)output);
            record("FONT.request.output.hash",hash);
            if (hash) {
                record("FONT.request.output.raw",ui_u32((void *)(hash+0x18)));
                record("FONT.request.output.command",ui_u32((void *)(hash+0x1c)));
                record("FONT.request.output.resource",((unsigned (*)(uintptr_t))(object_base+0x85bbd7))(hash));
            }
        }
        uintptr_t layout=((uintptr_t (*)(uintptr_t))(object_base+0x856f41))(ui_u32((void *)b));
        record("FONT.request.layout",layout);
        if (layout) {
            uintptr_t handler=ui_u32((void *)(layout+4));
            record("FONT.request.handler",handler);
            if (handler) {
                uintptr_t vptr=ui_u32((void *)handler);
                record("FONT.request.handler.vptr",vptr);
                record("FONT.request.image_count",((unsigned (*)(uintptr_t))(uintptr_t)ui_u32((void *)(vptr+12)))(handler));
            }
        }
    }
    return result;
}
static entry_fn original_multiline_complete, original_multiline_attach;
static unsigned multiline_complete_calls, multiline_attach_calls;
static uintptr_t observe_multiline_complete(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&multiline_complete_calls,1);
    if (n<=24) { record("FONT.multiline.complete",a);record("FONT.multiline.complete.index",b); }
    uintptr_t result=original_multiline_complete(a,b,c,d);
    if (n<=24) {
        record("FONT.multiline.complete.pending",ui_u32((void *)(a+0x114)));
        record("FONT.multiline.complete.ready",ui_u32((void *)(a+0x11c)));
    }
    return result;
}
static uintptr_t observe_multiline_attach(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&multiline_attach_calls,1);
    if (n<=24) {
        record("FONT.multiline.attach",a);
        record("FONT.multiline.attach.helpers",ui_u32((void *)(a+0x104)));
        record("FONT.multiline.attach.pending",ui_u32((void *)(a+0x114)));
        record("FONT.multiline.attach.ready",ui_u32((void *)(a+0x11c)));
    }
    return original_multiline_attach(a,b,c,d);
}
static uintptr_t observe_text_measure(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_measure_calls,1);
    uintptr_t result=original_text_measure(a,b,c,d);
    if (n<=24 || (n<=512 && b && c && (!ui_u16((void *)b) || !ui_u16((void *)c)))) {
        record("FONT.measure.ordinal",n);
        record("FONT.measure.request",a);record("FONT.measure.return",result);
        if (b) record("FONT.measure.width",ui_u16((void *)b));
        if (c) record("FONT.measure.height",ui_u16((void *)c));
        if (d) record("FONT.measure.format",ui_u32((void *)d));
        for (unsigned off=0;off<0x20;off+=4) {
            char key[48];snprintf(key,sizeof key,"FONT.measure.request+%02x",off);
            record(key,ui_u32((void *)(a+off)));
        }
    }
    return result;
}
static uintptr_t observe_image_allocate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_allocate_calls,1);
    uintptr_t result=original_image_allocate(a,b,c,d);
    if (n<=24 || (n<=1024 && result && !ui_u32((void *)(result+0x18)))) {
        record("FONT.allocate.ordinal",n);
        record("FONT.allocate.request",b);record("FONT.allocate.hash",result);
        record("FONT.allocate.request_vptr",ui_u32((void *)b));
        if (result) record("FONT.allocate.resource",((unsigned (*)(uintptr_t))(object_base+0x85bbd7))(result));
        if (result) record("FONT.allocate.raw_image",ui_u32((void *)(result+0x18)));
    }
    return result;
}
static unsigned font_image_result_calls;
static uintptr_t observe_image_result(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_image_result_calls,1);
    if (n<=48) {
        record("FONT.hash.assign",a);record("FONT.hash.assign.raw_image",b);
        record("FONT.hash.assign.kind",c);record("FONT.hash.assign.command",d);
        record("FONT.hash.assign.record",ui_u32((void *)(a+0x10)));
    }
    return original_image_result(a,b,c,d);
}
static unsigned font_child_calls, font_image_calls, font_convert_calls;
static uintptr_t font_children[64];
static unsigned font_children_count, font_text_child_logs;
static uintptr_t observe_child_draw(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_child_calls,1);
    int text_child=0;
    for (unsigned i=0;i<font_children_count;++i) text_child|=font_children[i]==a;
    if (n<=8 || (text_child && ++font_text_child_logs<=24)) {
        record("FONT.child.is_text",text_child);
        record("FONT.child.draw",a);
        record("FONT.child.visible",*(unsigned char *)(a+0x43));
        record("FONT.child.rect_count",ui_u16((void *)(a+0x8c)));
        record("FONT.child.image",ui_u32((void *)(a+0x50)));
        record("FONT.child.flags",ui_u16((void *)(a+0x5a)));
        uintptr_t rects=ui_u32((void *)(a+0x88));
        if (rects>=4096 && ui_u16((void *)(a+0x8c))) {
            record("FONT.child.rect_width",ui_u32((void *)(rects+8)));
            record("FONT.child.rect_height",ui_u32((void *)(rects+12)));
            record("FONT.child.src_width",ui_u32((void *)(rects+24)));
            record("FONT.child.src_height",ui_u32((void *)(rects+28)));
        }
    }
    return original_child_draw(a,b,c,d);
}
static uintptr_t observe_image_realize(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_image_calls,1);
    if (n<=12) {
        record("FONT.image.realize",a);record("FONT.image.layout",b);record("FONT.image.param",c);
        if (c) {
            record("FONT.image.width",((unsigned (*)(uintptr_t))(object_base+0x85a907))(c));
            record("FONT.image.height",((unsigned (*)(uintptr_t))(object_base+0x85a8eb))(c));
            record("FONT.image.resid",((unsigned (*)(uintptr_t))(object_base+0x85a85b))(c));
            for (unsigned off=0;off<=8;off+=4) {
                char label[48];snprintf(label,sizeof label,"FONT.image.data+%02x",off);
                record(label,ui_u32((void *)(c+off)));
            }
            uintptr_t hash=((uintptr_t (*)(uintptr_t))(object_base+0x85a815))(c);
            record("FONT.image.hash",hash);
            if (hash>=4096 && hash<0x60000000u-0x28) {
                record("FONT.image.hash.alias",((unsigned (*)(uintptr_t))(object_base+0x85bbb3))(hash));
                record("FONT.image.hash.record_type",((unsigned (*)(uintptr_t))(object_base+0x8542ab))(hash+0x10));
                for (unsigned off=0;off<0x28;off+=4) {
                    char label[48];snprintf(label,sizeof label,"FONT.image.hash+%02x",off);
                    record(label,ui_u32((void *)(hash+off)));
                }
            }
        }
    }
    uintptr_t result=original_image_realize(a,b,c,d);
    if (n<=12) {
        record("FONT.image.after.image",ui_u32((void *)(a+0x54)));
        record("FONT.image.after.rects",ui_u16((void *)(a+0x90)));
    }
    return result;
}
static uintptr_t observe_block_convert(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_convert_calls,1);
    if (n<=12) { record("FONT.block.convert",a);record("FONT.block.source",b);record("FONT.block.destination",c); }
    return original_block_convert(a,b,c,d);
}
static unsigned font_sprite_calls, font_face_calls, font_string_calls, font_chars_calls, font_raster_calls;
static uintptr_t observe_sprite_gate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_sprite_calls,1);
    if (n<=12) {
        record("FONT.sprite.enter",a);
        record("FONT.sprite.state",*(unsigned char *)(a+0x81));
        const unsigned offsets[]={0x84,0x88,0x8c,0x90,0x94,0x98,0x9c,0xa0,0xa4,0xa8};
        for (unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i) {
            char label[48];snprintf(label,sizeof label,"FONT.sprite+%03x",offsets[i]);
            record(label,ui_u32((void *)(a+offsets[i])));
        }
    }
    uintptr_t result=original_sprite_gate(a,b,c,d);
    if (n<=12) {
        record("FONT.sprite.return",result);
        record("FONT.sprite.state_after",*(unsigned char *)(a+0x81));
        uintptr_t vptr=ui_u32((void *)a);
        record("FONT.sprite.vptr",vptr);
        record("FONT.sprite.render_entry",ui_u32((void *)(vptr+0x74)));
        if (vptr==object_base+0x13b75e8) {
            record("FONT.multiline.area_width",((uintptr_t (*)(uintptr_t))(object_base+0x616653))(a));
            record("FONT.multiline.area_height",((uintptr_t (*)(uintptr_t))(object_base+0x61665b))(a));
            record("FONT.multiline.line_gap",((uintptr_t (*)(uintptr_t))(object_base+0x626add))(a));
            for (unsigned off=0x104;off<=0x138;off+=4) {
                char key[48];snprintf(key,sizeof key,"FONT.multiline+%03x",off);
                record(key,ui_u32((void *)(a+off)));
            }
            uintptr_t node=ui_u32((void *)(a+0x104));
            if (node>=4096 && node<0x60000000u-16) {
                uintptr_t helper=ui_u32((void *)(node+12));
                record("FONT.multiline.helper",helper);
                if (helper>=4096 && helper<0x60000000u-0xa4) {
                    uintptr_t child=helper+4;
                    if (font_children_count<64) font_children[font_children_count++]=child;
                    record("FONT.multiline.child",child);
                    record("FONT.multiline.child.vptr",ui_u32((void *)child));
                    record("FONT.multiline.child.image",ui_u32((void *)(child+0x50)));
                    record("FONT.multiline.child.alpha",*(unsigned char *)(child+0x43));
                    uintptr_t rects=ui_u32((void *)(child+0x88));
                    if (rects>=4096) {
                        record("FONT.multiline.child.width",ui_u32((void *)(rects+8)));
                        record("FONT.multiline.child.height",ui_u32((void *)(rects+12)));
                    }
                }
            }
        } else if (vptr==object_base+0x13b7dd0 && font_children_count<64) {
            font_children[font_children_count++]=a+0x108;
        }
        record("FONT.sprite.child.vptr",ui_u32((void *)(a+0x108)));
        record("FONT.sprite.child.visible",*(unsigned char *)(a+0x108+0x43));
        record("FONT.sprite.child.rect_count",ui_u16((void *)(a+0x108+0x8c)));
        const unsigned offsets[]={0xa4,0xa8,0xac,0xb0,0xb4,0xb8,0xbc,0x104,0x108,0x10c,0x110};
        for (unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i) {
            char label[48];snprintf(label,sizeof label,"FONT.sprite.after+%03x",offsets[i]);
            record(label,ui_u32((void *)(a+offsets[i])));
        }
    }
    return result;
}
static uintptr_t observe_face_select(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_face_calls,1);
    if (n<=12) {
        record("FONT.select.engine",a);
        record("FONT.select.mapper",ui_u32((void *)(a+4)));
        uintptr_t attr=ui_u32((void *)b);
        record("FONT.select.attr",attr);
        if (attr>=4096) {
            record("FONT.select.flags",ui_u16((void *)attr));
            record("FONT.select.requested",ui_u16((void *)attr)&1 ? ui_u32((void *)(attr+4)) : 0);
        }
    }
    uintptr_t result=original_face_select(a,b,c,d);
    if (n<=12 && c) record("FONT.select.face_handle",ui_u32((void *)c));
    return result;
}
static uintptr_t observe_string_id(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_string_calls,1);
    if (n<=12) { record("FONT.string.sprite",a);record("FONT.string.resource",b); }
    if (b == NE_TITLE) return native_entry_title(a,b,c,d);
    return original_string_id(a,b,c,d);
}
static uintptr_t observe_string_chars(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_chars_calls,1);
    if (n<=12) {
        record("FONT.chars.sprite",a);record("FONT.chars.pointer",b);
        record("FONT.chars.first_byte",b ? *(unsigned char *)b : 0);
    }
    return original_string_chars(a,b,c,d);
}
static uintptr_t observe_text_draw(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
{
    unsigned n=__sync_add_and_fetch(&font_raster_calls,1);
    if (n<=12) {
        record("FONT.raster.handle",a);record("FONT.raster.pixels",c);
        if (b) { record("FONT.raster.dimensions",ui_u32((void *)b));record("FONT.raster.stride",ui_u32((void *)(b+4))); }
        if (a) {
            record("FONT.raster.extended",*(unsigned char *)(a+0x1c));
            record("FONT.raster.block",*(unsigned char *)(a+0x1d));
        }
    }
    uintptr_t result=original_text_draw(a,b,c,d);
    if (n<=12) {
        record("FONT.raster.return",result);
        if (b && c) {
            size_t span=(size_t)ui_u16((void *)(b+2))*ui_u32((void *)(b+4));
            if (span && span<=1024*1024 && c<0x60000000u && span<=0x60000000u-c) {
                size_t nonzero=0;
                const unsigned char *pixels=(void *)c;
                for (size_t i=0;i<span;++i) nonzero+=pixels[i]!=0;
                record("FONT.raster.checked_bytes",span);
                record("FONT.raster.nonzero_bytes",nonzero);
            }
        }
    }
    return result;
}
static inline void inspect_font_counts(void)
{
    record("FONT.total.command_execute",font_execute_calls);
    record("FONT.total.command_post",font_post_calls);
    record("FONT.total.requests",font_request_calls);
    record("FONT.total.measures",font_measure_calls);
    record("FONT.total.rasters",font_raster_calls);
    record("FONT.total.sprite_gates",font_sprite_calls);
    record("FONT.total.image_attach",font_image_calls);
    record("FONT.total.multiline_complete",multiline_complete_calls);
    record("FONT.total.multiline_attach",multiline_attach_calls);
}
#define FONT_PROBE_HOOKS \
    {0x626374,{0xb0,0xb5,0x04,0x46,0x90,0xf8,0x81,0x30},observe_sprite_gate,&original_sprite_gate}, \
    {0x645d9c,{0x2d,0xe9,0xf0,0x47,0x88,0xb0,0x98,0x46},observe_face_select,&original_face_select}, \
    {0x626538,{0x90,0xb5,0x83,0xb0,0x00,0xaf,0x04,0x46},observe_string_id,&original_string_id}, \
    {0x6266d0,{0xf0,0xb5,0x00,0xf1,0x98,0x06,0x85,0xb0},observe_string_chars,&original_string_chars}, \
    {0x62c498,{0x2d,0xe9,0xf0,0x41,0x2d,0xed,0x02,0x8b},observe_child_draw,&original_child_draw}, \
    {0x626e88,{0x2d,0xe9,0xf0,0x41,0x2d,0xed,0x02,0x8b},observe_image_realize,&original_image_realize}, \
    {0x63cff0,{0x2d,0xe9,0xf0,0x4f,0x87,0xb0,0x0c,0x46},observe_block_convert,&original_block_convert}, \
    {0x85bc34,{0x80,0xb5,0x00,0xaf,0x81,0x61,0xc3,0x61},observe_image_result,&original_image_result}, \
    {0x85a534,{0x2d,0xe9,0xf0,0x43,0x85,0xb0,0x02,0xaf},observe_text_measure,&original_text_measure}, \
    {0x85499c,{0xf0,0xb5,0x8b,0xb0,0x02,0xaf,0x06,0x46},observe_image_allocate,&original_image_allocate}, \
    {0x6202ec,{0x2d,0xe9,0xf0,0x41,0x00,0xf5,0x8a,0x75},observe_multiline_complete,&original_multiline_complete}, \
    {0x62016c,{0x2d,0xe9,0xf0,0x47,0x00,0xaf,0x06,0x46},observe_multiline_attach,&original_multiline_attach}, \
    {0x856f78,{0x2d,0xe9,0xf0,0x4f,0x8d,0xb0,0x02,0xaf},observe_text_image_request,&original_text_image_request}, \
    {0x85a340,{0xb0,0xb5,0x88,0xb0,0x05,0x46,0x00,0xaf},observe_text_command_execute,&original_text_command_execute}, \
    {0x86033c,{0xb0,0xb5,0x82,0xb0,0x04,0x46,0x02,0xaf},observe_text_command_post,&original_text_command_post}, \
    {0x7c9ba0,{0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x02,0x8b},observe_slow_clear,&original_slow_clear}
#endif

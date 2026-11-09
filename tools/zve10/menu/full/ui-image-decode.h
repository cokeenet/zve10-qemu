/* AP software replacement for native LZ77 decoder B+0x63e170.
 * Source format is unchanged; original image-output write lease/events remain.
 */
#ifndef UI_IMAGE_DECODE_H
#define UI_IMAGE_DECODE_H
#include <stdlib.h>
#ifndef UI_IMAGE_FN
#define UI_IMAGE_FN(offset,type) ((type)(object_base+(offset)))
#endif

static __attribute__((noreturn)) void image_decode_fail(const char *phase, uintptr_t value)
{
    record("LDEC.cpu.UNSUPPORTED",value);
    record(phase,value);
    _exit(71);
}
static uintptr_t image_decode_method(uintptr_t object, unsigned offset)
{
    if (object < 4096 || object > 0x60000000u-4)
        image_decode_fail("LDEC.cpu.interface",object);
    uintptr_t vptr=ui_u32((void *)object);
    if (vptr < 4096 || vptr > 0x60000000u-offset-4)
        image_decode_fail("LDEC.cpu.vtable",vptr);
    uintptr_t method=ui_u32((void *)(vptr+offset));
    if (method < 4096 || method >= 0x60000000u)
        image_decode_fail("LDEC.cpu.method",method);
    return method;
}
static uintptr_t software_lz_decode(uintptr_t decoder, uintptr_t b, uintptr_t c, uintptr_t d)
{
    (void)b; (void)c; (void)d;
    const unsigned char *p=(void *)decoder;
    unsigned format=ui_u32(p+12), width=ui_u16(p+16), height=ui_u16(p+18);
    size_t length=ui_u32(p+20);
    if (!object_base || format < 0x300 || format > 0x304 || !width || !height ||
        width > 8192 || height > 8192 || !length || length > 64u*1024*1024)
        image_decode_fail("LDEC.cpu.decoder_shape",format);
    uintptr_t input=ui_u32(p+4), output=ui_u32(p+8);
    uintptr_t source=((uintptr_t (*)(uintptr_t))image_decode_method(input,0x2c))(input);
    uintptr_t bitmap=((uintptr_t (*)(uintptr_t))image_decode_method(output,0x30))(output);
    if (source < 4096 || source >= 0x60000000u || length > 0x60000000u-source ||
        bitmap < 4096 || bitmap > 0x60000000u-12 ||
        ui_u32((void *)bitmap) != object_base+0x13ba5a0)
        image_decode_fail("LDEC.cpu.source_or_bitmap",bitmap);
    uintptr_t metadata=ui_u32((void *)(bitmap+4));
    if (metadata < 4096 || metadata > 0x60000000u-0x34)
        image_decode_fail("LDEC.cpu.metadata",metadata);
    unsigned char before[0x34]; memcpy(before,(void *)metadata,sizeof before);
    int16_t bpp=0;
    unsigned native_bits=UI_IMAGE_FN(0x862a69,unsigned (*)(unsigned))(format);
    static unsigned metadata_reports;
    if (__sync_add_and_fetch(&metadata_reports,1) <= 8) {
        record("LDEC.cpu.native_bits",native_bits);
        for (unsigned i=0;i<sizeof before;i+=4) record("LDEC.cpu.output_metadata",ui_u32(before+i));
    }
    if (UI_IMAGE_FN(0x69d145,int (*)(uint32_t,int16_t *))(ui_u32(before+0x1c),&bpp) ||
        bpp <= 0 || (unsigned)bpp*8 != native_bits || (bpp != 1 && bpp != 2 && bpp != 4) ||
        ui_u32(before+4) || ui_u32(before+0x14) || ui_u32(before+0x30) ||
        ui_u32(before+0x2c) != 1 || ui_u16(before+8) != width || ui_u16(before+10) != height)
        image_decode_fail("LDEC.cpu.output_format",ui_u32(before+0x1c));
    unsigned aw=(width+7)&~7u, ah=(height+7)&~7u;
    size_t span=(size_t)aw*ah*(unsigned)bpp;
    uint32_t physical=ui_u32(before+0x24);
    if (ui_u16(before+0x18) != aw || ui_u16(before+0x1a) != ah || span > 64u*1024*1024 ||
        physical < 0x20000000u || physical > 0x28000000u-span)
        image_decode_fail("LDEC.cpu.output_span",span);
    unsigned char *decoded=malloc(span);
    if (!decoded) image_decode_fail("LDEC.cpu.allocate",span);
    int status=ui_lz77_decode((void *)source,length,decoded,span);
    if (status) { free(decoded); image_decode_fail("LDEC.cpu.stream",length); }
    uint32_t stride=0;
    uintptr_t bits=UI_IMAGE_FN(0x863889,uintptr_t (*)(uintptr_t,uint32_t *))(output,&stride);
    if (!bits) { free(decoded); image_decode_fail("LDEC.cpu.write_lock",bitmap); }
    UI_IMAGE_FN(0x644df1,void (*)(unsigned,uintptr_t,int,int))(4,bitmap,0,0);
    if (bits < 4096 || bits >= 0x60000000u || span > 0x60000000u-bits ||
        bits != ui_u32((void *)(bitmap+8)) || stride != aw*(unsigned)bpp ||
        ui_u32((void *)(bitmap+4)) != metadata || memcmp(before,(void *)metadata,sizeof before)) {
        status=-5;
    } else {
        memcpy((void *)bits,decoded,span);
        __sync_synchronize();
    }
    UI_IMAGE_FN(0x644df1,void (*)(unsigned,uintptr_t,int,int))(8,bitmap,status,status != 0);
    UI_IMAGE_FN(0x86389b,void (*)(uintptr_t))(output);
    free(decoded);
    if (status) image_decode_fail("LDEC.cpu.changed_output",bitmap);
    record("LDEC.cpu.decoded",span);
    return 1;
}
#endif

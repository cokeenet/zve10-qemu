/* Bounded adaptation of work/linux-kernel/lib/lz77/lz77_inflate.c and
 * include/linux/lz77.h. Format and length table remain Sony's original.
 *
 * Copyright 2006,2007 Sony Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef UI_LZ77_H
#define UI_LZ77_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Decode to private scratch: failure may leave a prefix, never publish it. */
static inline int ui_lz77_decode(const unsigned char *src, size_t ss,
                                 unsigned char *dst, size_t ds)
{
    if (!src || !dst || ss < 4 || !ds) return -5;
    uintptr_t s=(uintptr_t)src, d=(uintptr_t)dst;
    if (s <= d ? d-s < ss : s-d < ds) return -5;
    static const unsigned char lengths[16]={3,4,5,6,7,8,9,10,11,12,13,14,15,16,32,64};
    size_t in=0, out=0;
    while (in < ss) {
        size_t block=out;
        unsigned method=src[in++];
        if (method == 0x0f) {
            if (ss-in < 3) return -5;
            size_t length=src[in+1] | (size_t)src[in+2]<<8;
            in+=3;
            if (!length || length > ds-out || length > ss-in) return -5;
            memcpy(dst+out,src+in,length);
            in+=length; out+=length;
        } else if (method == 0xf0) {
            int ended=0;
            while (in < ss && !ended) {
                unsigned flags=src[in++];
                for (unsigned bit=0;bit<8;++bit) {
                    if (flags & (1u<<bit)) {
                        if (ss-in < 2) return -5;
                        unsigned count=lengths[src[in]>>4];
                        unsigned distance=((src[in]&15u)<<8)|src[in+1];
                        in+=2;
                        if (!distance) { ended=1; break; }
                        if (distance > out-block || count > ds-out) return -5;
                        while (count--) { dst[out]=dst[out-distance]; ++out; }
                    } else {
                        if (in==ss || out==ds) return -5;
                        dst[out++]=src[in++];
                    }
                }
            }
            if (!ended || out==block) return -5;
        } else return -5;
    }
    return out==ds ? 0 : -5;
}
#endif

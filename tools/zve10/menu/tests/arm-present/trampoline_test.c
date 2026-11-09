#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "full/ui-present.h"
typedef uintptr_t (*entry_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t object_base;
static uint32_t ne_packed;
static int mapped_overlay_calls;
static int native_entry_present(void *p,size_t n,uint32_t phys,uintptr_t caller) {
 (void)p;(void)n;(void)phys;(void)caller; ++mapped_overlay_calls; return 0;
}
#define nm_log(...) fprintf(stderr,__VA_ARGS__)
/* Use the exact production jump emitter. */
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
#include "native_present_adapter.h"
#include "native_present_install.inc"
extern const unsigned char submit_fixture_start[],submit_fixture_end[];
extern const unsigned char wait_fixture_start[],wait_fixture_end[];
int main(void) {
 unsigned char *image=mmap(NULL,0x780000,PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(image!=MAP_FAILED);
 object_base=(uintptr_t)image;
 size_t submit_len=submit_fixture_end-submit_fixture_start;
 size_t wait_len=wait_fixture_end-wait_fixture_start;
 memcpy(image+0x774ef0,submit_fixture_start,submit_len);
 memcpy(image+0x775120,wait_fixture_start,wait_len);
 assert(mprotect(image,0x780000,PROT_READ|PROT_EXEC)==0);
 entry_fn submit=(entry_fn)((uintptr_t)(image+0x774ef0)|1u);
 entry_fn wait=(entry_fn)((uintptr_t)(image+0x775120)|1u);
 uint32_t request[14]={0};
 assert(submit((uintptr_t)request,2,3,4)==77);
 assert(wait(2,2,3,4)==0x40000000);
 assert(wait(0,2,3,4)==0); /* relative branch taken in original wait */
 puts("ARM_FIXTURE_BASELINE_PASS");
 assert(nm_install_present_pair()==0);
 assert(nm_original_present && nm_original_engine_wait);
 assert(submit((uintptr_t)request,2,3,4)==77);
 assert(wait(2,2,3,4)==0x40000000);
 assert(wait(0,2,3,4)==0); /* branch still reaches original target */
 assert(mapped_overlay_calls==0);
 puts("ARM_TRAMPOLINE_RETURN_AND_BRANCH_PASS");
 assert(nm_install_present_pair()==-1); /* occupied entry must be refused */
 puts("ARM_OCCUPIED_PIN_REFUSED_PASS");
 assert(munmap(image,0x780000)==0);
 return 0;
}

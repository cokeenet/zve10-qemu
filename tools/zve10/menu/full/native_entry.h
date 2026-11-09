#ifndef ZVE10_NATIVE_ENTRY_H
#define ZVE10_NATIVE_ENTRY_H
#include <stddef.h>
#include <stdint.h>
#define NE_TITLE UINT32_C(0x02feff01)
struct headless_snapshot;
#include "transport.h"
static void native_entry_modules(void *handle);
void native_label_text(void *label, uint32_t id) __asm__("_ZN9GEN_Label7setTextEj");
static uintptr_t native_entry_title(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);
static void native_entry_after_draw(void);
static int native_entry_present(void *pixels, size_t length, uint32_t physical, uintptr_t caller);
#endif

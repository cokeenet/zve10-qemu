/* mini_font.h —— 极简字体钩子声明。
 * 原 ui-font-probe.h 装了 16 个 hook；native_entry 只需 2 个。 */
#ifndef MINI_FONT_H
#define MINI_FONT_H

static uintptr_t observe_string_id(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);
static uintptr_t observe_string_chars(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

#endif

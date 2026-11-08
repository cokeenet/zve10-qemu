/* transport.h —— 真机版最小 transport。
 * 原版把状态/帧经 UART 送 QEMU host；真机没有 host，只保留类型定义。 */
#ifndef ZVE10_NATIVE_ENTRY_TRANSPORT_H
#define ZVE10_NATIVE_ENTRY_TRANSPORT_H
#include <stdint.h>

struct ne_observation {
    uint32_t pid, ui_tid, view, view_state, submode, parent, count;
    uint32_t selected_index, selected_own, node_installed, title_sets;
    uint32_t overlay, category, entries, exits, last_input;
};

static uint32_t ui_tid;

static void ne_transport_cache(const void *, unsigned, uint64_t);
static void ne_transport_retired(uint64_t);
static void ne_transport_ui(const struct ne_observation *, uint64_t);
static void ne_transport_start(void);

#endif

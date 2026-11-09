#ifndef ZVE10_NATIVE_ENTRY_TRANSPORT_H
#define ZVE10_NATIVE_ENTRY_TRANSPORT_H

#include <stdint.h>

struct headless_snapshot;
static volatile int ne_transport_fd = -1;
struct ne_observation {
    uint32_t pid, ui_tid, view, view_state, submode, parent, count;
    uint32_t selected_index, selected_own, node_installed, title_sets;
    uint32_t overlay, category, entries, exits, last_input;
};

static void ne_transport_cache(const struct headless_snapshot *, unsigned,
                               uint64_t frame);
static void ne_transport_retired(uint64_t frame);
static void ne_transport_ui(const struct ne_observation *, uint64_t frame);
static void ne_transport_start(void);

#endif

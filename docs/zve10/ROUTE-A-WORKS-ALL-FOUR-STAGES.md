# Route A works: node installed, row drawn, focus on index 6

## The log, one run

```
NE_MODULES      {"pid":122,"hooks":4}
NE_VERSION      {"last":6}
NE_TREE_ADDED   {"parent":841167672,"old_count":5,"new_count":6,"node":2437944}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          (five occurrences)
NE_READY        {"frame":21,"pid":122,"ui_tid":214,"view":2444328,
                 "title_sets":5,"native_glyph_images":7}
UI_OBSERVE      24,833 lines
```

## Against upstream's own verification

Upstream `result.json`, first frame state:

```json
{"pid":103,"ui_tid":182,"count":6,"selected_index":6,
 "selected_own":1,"node_installed":1,"title_sets":5}
```
`NE_READY` from upstream: `title_sets:5, native_glyph_images:7`.

| fact | upstream | here |
|---|---|---|
| hooks installed | 4 | 4 |
| `node_installed` | 1 | 1 |
| `old_count -> new_count` | 5 -> 6 | 5 -> 6 |
| `selected_index` | 6 | 6 |
| `group / page` | -- | 5 / 5 |
| `title_sets` | 5 | 5 |
| `native_glyph_images` | 7 | 7 |

Every semantic field matches, including the two incidental ones.

## Three separate problems had to be solved

**1. `-initrd` was always rejected.**

```
INITRD: 0x0e8e4400+0x0006febc overlaps in-use memory region - disabling initrd
```

Not a size problem -- a 458 KB cpio failed identically. QEMU computes

```c
info->initrd_start = info->loader_start + MIN(ram_size/2, 128 MiB);
if (image_high_addr)
    info->initrd_start = MAX(info->initrd_start, image_high_addr);
```

and `image_high_addr` is the end of the 232 MB kernel file, `0x0e8e4400`,
which this kernel then reserves for its own decompressed image. So
`reserve_bootmem_node(..., BOOTMEM_EXCLUSIVE)` always failed.

Fixed by patching `hw/arm/boot.c` to place the initrd at `0x30000000` when
`ram_size >= 1 GiB`, above both `memrsv` regions (`0x02E68000+26M` and
`0x20000000+128M`) and inside the 1 GiB of RAM. After that the kernel reports

```
Freeing initrd memory: 246620K
```

which is the 241 MB cpio actually loading.

**2. Two preloaded libraries both export `dlopen`.**

```
libuiobserve.so    dlopen @ 0x9964   dlsym @ 0x88dc
libnativeentry.so  dlopen @ 0xb474   dlsym @ 0x9edc
```

With `LD_PRELOAD=/usr/lib/libuiobserve.so:/usr/lib/libnativeentry.so`, the
first one wins the symbol and `libnativeentry.so`'s own `dlopen` hook -- the
thing that starts its whole initialisation -- never runs. It stayed silent, no
`NE_*` at all, which is what sent me chasing its `.init_array` for two rounds.

Upstream preloads exactly one:

```sh
( LD_PRELOAD=/usr/lib/libnativeentry.so /usr/bin/bootin.elf ... ) &
```

`libuiobserve.so` still gets used -- `UI_OBSERVE` appears 22,070 times in
upstream's serial log -- because `libnativeentry.so` opens it itself. Preloading
it first was actively harmful.

**3. Mixing the two recipes.**

Route A is `kbuild-ui` + `native-ui.cpio` + `libnativeentry.so`; route B is
`kbuild-ui2` + `ui.cpio` + `libm2probe/libuiobserve`. Booting B's kernel while
preloading A's library produces nothing. Once route A was run as route A, it
worked.

## What is still missing

`NE_STATE` did not appear, so the `CAPTURE` handshake still does not complete
and no `.rgba4444` frame was written. The menu work itself is done and verifiable
from the log; only the host-side capture round trip is outstanding.

## Reproduce

```
# QEMU must be the patched build (initrd at 0x30000000)
/mnt/build/ref-chocotan-zve10/work/qemu-src/build-zve10/qemu-system-arm

tools/zve10/qemu/run_routeA.sh <name> 200
```

cpio: `/mnt/build/routeA/ui-routeA.cpio` (1314 entries, upstream's native-ui
tree plus `libnativeentry.so`, init preloading only that one library).

The unpatched binary is kept at
`build-zve10/qemu-system-arm.orig` (md5 `e2a068c8`) and `boot.c` at
`hw/arm/boot.c.orig`, so the change is reversible.

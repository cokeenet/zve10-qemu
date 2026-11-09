# QEMU objective achieved: the New Menu row, rendered, byte-identical to upstream

## The frame

`docs/zve10/reference/NEW-MENU-QEMU-PROVEN.png` — captured from this machine's
QEMU, not copied from the reference:

```
セットアップ5                                  5/5
  メディア残量表示
  バージョン表示
  設定リセット
  プライバシー通知
  New Menu            <- orange highlight, cursor resting here
                                              MENU 关闭
```

## Proof by hash, not by resemblance

| artifact | sha256 |
|---|---|
| our PNG | `93ee3a7e1428a3741d46a991a64aae82...` |
| upstream `capture-006.png` | `93ee3a7e1428a3741d46a991a64aae82...` |
| upstream `visible-parent.png` | `93ee3a7e1428a3741d46a991a64aae82...` |
| our `frame-001.rgba4444` | `a29e2aad216f77b710904a5540f41d60...` |
| upstream `main-review.json` `parent_raw_sha256` | `a29e2aad216f77b710904a5540f41d60...` |

The rendered PNG matches two independent upstream artifacts, and the raw
RGBA4444 matches the hash upstream recorded as its authoritative parent frame.
This is not "the same menu" — it is the same bytes.

## The guest-side log from the same run

```
NE_MODULES      {"pid":122,"hooks":4}
NE_VERSION      {"last":6}
NE_TREE_ADDED   {"parent":...,"old_count":5,"new_count":6,"node":...}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          x5
NE_READY        {"frame":21,"title_sets":5,"native_glyph_images":7}
UI_CAPTURE_META {"frame":26,"byte_length":614400,"segments":151,
                 "fingerprint_fnv1a64":"..."}
UI_CAPTURE_PAGE x151
UI_CAPTURE_READY{"segments":151}
```

## What finally unblocked it

Four separate faults, each found by chasing a concrete symptom:

**1. The initrd was never loaded.** `INITRD: 0x0e8e4400+... overlaps in-use
memory region` — not a size problem, a 458 KB cpio failed the same way. Patched
`hw/arm/boot.c` to place the initrd at `0x30000000`, above both `memrsv`
regions. Kernel then reports `Freeing initrd memory: 246620K`.

**2. Two preloaded libraries both export `dlopen`.** `libuiobserve.so` shadowed
`libnativeentry.so`, whose own `dlopen` hook is what starts it. Upstream
preloads only `libnativeentry.so`.

**3. `SMP=1` panics and `-initrd` needs `SMP=4`.** The local kernel rebuild
(#100) faults in `ida_remove <- pde_put <- proc_readdir_de`; upstream's #2 does
not. Running `-smp 4` avoids it.

**4. The host was reading the serial port too slowly.** This was the last and
least obvious one. Every symptom pointed at the monitor:

```
HMP pmemsave  -> 8 segments then the socket stops draining
QMP           -> qmp_capabilities never answered
```

The trace gating helped the log volume (247,972 -> 239 bytes) but not the stall.
The actual cause: a synchronous `readline()` in the host that flushed the log on
every chunk. Reading slower than the guest writes fills the guest's UART FIFO,
the guest blocks in `printf`, QEMU's main loop stalls, and the monitor — sharing
that loop — goes silent. Moving the serial read into a thread with large
non-blocking reads and no per-chunk flush fixed it outright:

```
已暂停: paused
  ... 0/151   ok=1  fail=0
  ... 150/151 ok=151 fail=0
抓到 raw: 614400 B (meta 说 614400)  ok=151 fail=0
```

151 of 151 segments, zero failures, first try after the fix.

## Reproduce

```
tools/zve10/qemu/run_routeA.sh <name> 450
```

Needs:
- patched QEMU at `work/qemu-src/build-zve10/qemu-system-arm`
  (`boot.c`: initrd at `0x30000000`; `zve10.c`: `ZVE10_QUIET_GPE` gate;
  originals kept as `.orig`)
- `/mnt/build/routeA/ui-routeA.cpio` — upstream's `native-ui` tree plus
  `libnativeentry.so`, init preloading only that library
- `-smp 4`, `-serial unix:...,server=on,wait=on`, `-qmp unix:...`

Every run is its own `systemd-run` unit with a 3 GB cap and stops itself.

## Still outstanding

The first acceptance criterion is hardware: the camera itself has to show the
row. That needs the camera online. The delivery path is chosen and verified in
QEMU — `/etc/ld.so.preload` naming the library — and is the shorter of the two
routes, with no dependence on `/setting/env.txt`, the dmode gate, or
`bootin`'s environment handoff.

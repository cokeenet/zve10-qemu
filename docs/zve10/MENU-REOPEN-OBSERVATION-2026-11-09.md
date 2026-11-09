# QEMU menu close/reopen observation: close verified, reopen unresolved

## Setup

An independent copy of menu/full source was built under
/mnt/build/menu-lifecycle-_k2io04e/source. Added one bounded observation in
ne_init_wrapper when caller == views_base+0x301921, recording invocation count,
root and thread. No change to insertion or lifecycle semantics.

A tiny appended cpio overlays init and a separately named observer DSO onto the
existing route-A archive. Original kernel, archives, root init and deployed camera
files were not changed. Each guest used systemd-run --collect --no-block,
MemoryMax=3G and RuntimeMaxSec 165–320. A fast threaded serial reader drains UART.

## Three runs, accurately classified

1. Initial observer: one lifecycle initialization, row 5->6 and focus. No NE_READY
   within the 220-second observation window, so no keys sent. Failed readiness,
   not a lifecycle close/reopen test.
2. Added readiness-field logging without relaxing its predicate. NE_READY reached;
   ENTER then MENU yielded overlay 1 then 0 in the same native view. Next MENU
   was consumed by native menu as action 15, after which current view was zero.
   Host requested CAPTURE immediately and blocked waiting for a capturable frame.
   Thus the next key was never sent. No reopen claim follows.
3. Host deferred CAPTURE between native close/reopen key attempts. All four keys
   were queued: ENTER, MENU, MENU, MENU. The first two produced expected states.
   Native close again removed current menu view. The last MENU was acknowledged
   as enqueued but produced no second initialization and no capturable frame.

The key distinction: NE_KEY_SENT proves enqueue, not application-level reopen.
The original-menu return path was observed; the off-menu event path is unresolved.

## Relevant observed fields from final run

```
NE_LIFECYCLE_INIT call=1 root=841017672 installed=0 tid=213
NE_READY frame=21 view=2426512 title_sets=5 glyphs=7
seq201 ENTER: overlay=1 entries=1 exits=0 view=2426512
seq202 MENU:  overlay=0 entries=1 exits=1 view=2426512
seq203 MENU:  native_action=15
seq204 MENU:  NE_KEY_SENT only
readiness after close: latest_valid=0 latest=29 retired=30 observed=30 view=0
```

Each run recorded exactly one target initialization call; the latter two had
NE_READY, and no NE_ERROR record was observed. The global frame/capture predicate
waited after close rather than declaring an invalid menu frame ready.

## Limits

This run used the full QEMU observer and its software rendering/input scaffolding,
not the hardware slim or late-loaded experimental presentation adapter.
It does not establish factory cache behavior, late-load insertion correctness,
UI-thread patch quiescence, or a valid close/rebuild command on the camera.
No real camera request, memory write, restart or deployment took place.

## Evidence and cleanup

Complete sources, cpio overlays, host scripts, serial logs/events and result.json
remain in /mnt/build/menu-lifecycle-_k2io04e/. result.json records SHA256/size for
all three serial logs and marker counts. All three systemd units were stopped or
already collected; each was checked inactive. No qemu-system-arm remained.

Next work is identifying the actual off-menu input owner/native menu-open
operation in the guest; do not conclude that two MENU key enqueues constitute
close plus reopen, and do not call initialization methods on a live object as a
shortcut.

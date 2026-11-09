# Deferred row insertion: full acceptance chain re-verified in QEMU

## Setup

Independent copy of menu/full, hooks installed at startup, row insertion armed
only when the UI thread consumes the one-shot OPEN_NATIVE request. The test
transport uses NE_BASELINE_READY (deliberately distinct from NE_READY) so the
unmodified baseline menu can be observed before any owned node exists.

Kernel: existing kbuild-ui Image. Base archive: existing route-A cpio. A small
appended cpio overlays only init and one new DSO name. Camera, route-A source
tree and reference archives were not modified. Each systemd unit used
`--collect --no-block`, MemoryMax=3G, RuntimeMaxSec=250.

## Observed sequence (single run)

```
call 1  lifecycle init, root=841234760, installed=0
        baseline capture 400: node_installed=0, selected_own=0, count=6
MENU 401 native close
OPEN_NATIVE queued -> consumed on UI tid 214
call 2  lifecycle init, installed=0
NE_TREE_ADDED old_count=5 -> new_count=6
capture 402: view=2742744 count=6 selected_index=6 selected_own=1
             node_installed=1 title_sets=3 overlay=0
ENTER 403 -> capture 403: overlay=1 category=7 entries=1 selected_own=1
MENU  404 -> capture 404: overlay=0 exits=1 selected_own=1
MENU  405 native close
OPEN_NATIVE again -> consumed on UI tid 214
call 3  lifecycle init, installed=1   (no second NE_TREE_ADDED)
capture 406: view=2313168 count=6 selected_index=6 selected_own=1
             node_installed=1 title_sets=8 overlay=0
```

Measured facts: 3 lifecycle initializations on the UI thread, exactly 1
NE_TREE_ADDED, 0 NE_ERROR lines, ENTER after insertion produced overlay=1 with
the new category and MENU returned to overlay=0, and a second full close/reopen
re-initialized without duplicating the child while keeping selected_own=1.

## What this establishes, and what it does not

Established for this controlled QEMU environment: the verified native
reconstruction path can run again after menus were already built, the existing
pre-init insertion routine appends correctly at that moment, and the appended
row remains selectable/enterable and survives a later close/reopen without
duplication.

Not established: live hook installation on the camera (all patching preceded UI
startup), real GPU presentation behavior, an actual camera call to this native
request, or pixel-level comparison in this lifecycle run. The full observer and
its software graphics scaffolding were used, not the slim hardware library.

Hardware acceptance (row, ENTER, MENU on the camera) remains unfulfilled and no
completion is claimed.

## Evidence

Copies remain in /mnt/build/menu-deferred-7ac_eel3/: serial log (1,314,345 B,
sha256 e4e89334…), events JSON, host script, untouched source snapshots and the
built overlay DSO (214,380 B, sha256 4fbde86b…). result-final.json records all
identities. The unit was inactive after the run; no qemu-system-arm remained.

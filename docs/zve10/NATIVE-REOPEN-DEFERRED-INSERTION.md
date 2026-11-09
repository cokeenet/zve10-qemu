# Native UI-thread reopen and deferred row insertion measured in QEMU

## Scope

Independent full-observer experiments, not a slim late-load hardware test.
Hooks install at startup. Real camera, deployment configuration and reference
archives were not modified. No hardware reboot or UI restart was issued.

## Native open request

Existing full observer try_original_menu validates ViewManager, required fields,
root view vptrs and root->manager; it then performs the native menu-open request,
with the existing Wi-Fi guard. Added a one-shot OPEN_NATIVE transport command
that only sets a request flag. The existing UI draw callback consumes the flag
and reruns try_original_menu. The serial reader does not call UI methods itself.

Experiment /mnt/build/menu-open-3v13qe89/:

- initial NE_READY, normal ENTER overlays new menu, MENU returns to parent
- another MENU is consumed as native_action=15 and closes native menu
- OPEN_NATIVE is queued, then consumed on UI tid 212
- NE_LIFECYCLE_INIT call=2 on that same thread
- native menu becomes observable again with count=6, selected_own=1
- view/root addresses equal their pre-close values in this run

This proves native initialization can be repeated through the demonstrated
UI-thread request path. It does not prove every user MENU reopen uses a new
allocation; the observed object was reused in this run.

## Deferred-insertion experiment

Independent source copy at /mnt/build/menu-deferred-7ac_eel3/ gates ne_add_row
with deferred_insert_armed, initially false. Hooks remain installed at startup.
The UI thread arms insertion only when it consumes OPEN_NATIVE after menu close.

To observe the original menu before any owned row exists, the test transport
uses a deliberately different marker NE_BASELINE_READY. Its initial readiness
allows an observed native view without the owned-node/glyph acceptance checks.
That marker is explicitly NOT NE_READY and is not treated as normal acceptance.
Baseline UI observation also skips owned-widget binding until a node is installed.

Observed:

```
NE_LIFECYCLE_INIT call=1 root=831981896 installed=0 tid=214
baseline capture_id=400: node_installed=0 selected_own=0 title_sets=0
native MENU close seq401
OPEN_NATIVE queued
UI tid214 consumes reopen
NE_LIFECYCLE_INIT call=2 root=831981896 installed=0 tid=214
NE_TREE_ADDED parent=831947576 old_count=5 new_count=6 node=2212304
capture_id=402: view=2763320 view_state=1 count=6 selected_index=6
               selected_own=1 node_installed=1 title_sets=3 overlay=0
```

The baseline parent (831959716) differs from the post-insertion parent. Its count
must not be compared as if it represented the same page. The actual mutation is
established by NE_TREE_ADDED's old/new counts and post-init owned-node selection.
Root was reused; view address changed between baseline and reopen in this run.

## What this establishes

The existing pre-init insertion routine can append correctly even after a prior
unmodified native initialization, PROVIDED the verified native reconstruction
runs again on the UI-owning thread after insertion is armed.

## What it does not establish

- Live hook installation is not tested: all code patches preceded UI startup.
- No ptrace remote loader or multi-thread quiescence protocol was executed.
- Uses full QEMU observer and software graphics scaffolding, not experimental
  hardware presentation. No real GPU/cache-coherence qualification.
- No screenshot/raw-pixel comparison was taken in this lifecycle experiment.
- Deferred run checks lifecycle and owned selection, not a fresh ENTER/MENU pair
  after insertion. Earlier baseline overlay behavior is separate evidence.
- The same native request has not been safely called in the real camera UI.

This removes the "no known catch-up lifecycle" uncertainty for this controlled
software UI. A safe deployment path still needs either fresh UI exec or a
qualified, quiescent runtime installer plus a UI-thread request mechanism.

## Evidence and resource cleanup

Each directory contains exact source, DSO, overlay cpio, host script, serial log,
events and result.json. Result manifests include SHA256 identities. Both runs
used systemd-run --collect --no-block, MemoryMax=3G, RuntimeMaxSec=245.
Both units were stopped and checked inactive. No qemu-system-arm remained.

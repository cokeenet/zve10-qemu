# QEMU and the camera differ at exactly one link: libuiobserve

## The experiment

Removed `libuiobserve.so` from the rootfs and from `LD_PRELOAD`, leaving
`libm2probe.so` and `libnewmenu.so`, to imitate a camera where nothing hooks the
UI:

```
Memory:            669304k available      (layout fine)
dlopen:            123                    (CP passed)
viewUnified2:      loaded, views_base=0x31790000
hooked CautionConfig init
hooked viewUnified2 (3)
installed object=0x9b6b000 views=0x31790000 modules=1

string_id hook failed:    0    <- the font hooks now install
string_chars hook failed: 0
NE_TREE_ADDED:            0    <- but nothing calls initSettingNode
NE_FOCUS_ENTRY:           0
```

So removing it does clear the font-hook collision, exactly as predicted — and it
also stops the menu from ever being built.

## Why

`libuiobserve.so` carries:

```
observe_notify
notifyDraw.enter / notifyDraw.return
notify_count
```

It hooks the draw notification and keeps the UI stepping. Without it the interface
never reaches the Settings page, so `CmnViewSettingNode::initSettingNode` is never
called, so `ne_init_wrapper` never runs, so `ne_add_row` never fires. All six hooks
are installed and idle.

`KEYINJECT` is *not* from this library — it survives removal and comes from a
separate `libkeyinject.so`.

## Why that does not transfer to the camera

| | QEMU | camera |
|---|---|---|
| UI driven by | `libuiobserve.so` hooking `notifyDraw` | real hardware, real interrupts |
| needs `libm2probe.so` | yes, it papers over a CP reply that never arrives | no, the CP is real |
| needs `libuiobserve.so` | yes, or the UI stalls | no, nothing to stall it |
| font-hook slots | already taken by libuiobserve | free, so our hooks install |
| Settings page reached by | the observer's draw loop | the user pressing MENU |

The camera deployment ships `libnewmenu.so` alone. Everything QEMU needs to
simulate a working CP and a stepping UI is scaffolding for a machine that does not
need scaffolding.

## What this means for confidence

It removes the last borrowed assumption. Earlier rounds argued "the font hooks
fail here because libuiobserve took them, so they should succeed on the camera" —
that was inference. The removal experiment turns it into a measurement: with the
collision gone the hooks install, `NM installed … modules=1` with zero warnings.

The other half — that the camera actually calls `initSettingNode` — cannot be
measured here, because in QEMU that call only happens as a side effect of the
observer's draw loop. It rests on two things instead: the gate address
`views_base + 0x301921` is the return site of a call that builds the settings page,
and on the camera that page is built when the user opens the menu, which is the
whole point of the feature.

## Restored

`libuiobserve.so` goes back, along with the three-library `LD_PRELOAD`, because
route B needs it to reach the page at all. The camera package never included it.

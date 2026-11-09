# The upstream reference, mapped out

## What is actually in `native-window-v14/`

I had been treating this directory as "19 screenshots". It is a complete,
self-documenting build:

```
observer-derived.c      30,917 B   source of libnativeentry.so
native_entry.inc        20,762 B   the menu hooks
menu_shell.c                       in a7c2-assets/guest/
transport.inc           20,694 B   the CAPTURE / KEY / ACK serial protocol
ui-headless.h           13,428 B   headless render
ui-present.h, ui-raster.h, ui-draw-probe.h, ui-font-probe.h,
ui-gemini-backend.h, ui-gemini-raster.h, ui-image-decode.h,
ui-lz77.h, ui-slow-clear.h
native_entry.h, native_pins.h, transport.h
libnativeentry.so      214,236 B   the built library
native-ui.cpio     252,544,512 B   the initramfs passed with -initrd
overlay.cpio, overlay.spec
serial.log           1,658,901 B   the full QEMU serial transcript
qemu.log             1,152,572 B
result.json             22,772 B
main-review.json           663 B
capture-001..019.png + .rgba4444   nineteen frames, lossless
visible-initial.png, visible-parent.png, visible-new-menu.png
reference-replay       128,264 B
reference-category-6.rgba4444, reference-category-7.rgba4444
```

`native-ui.cpio` hashes to `32803dee...`, matching `runtime_cpio_sha256` in
`build.json`. So it is the artifact that actually ran, not a leftover.

## The target frame

`visible-parent.png` shows what the objective asks for:

```
セットアップ5                                  5/5
  メディア残量表示
  バージョン表示
  設定リセット
  プライバシー通知
  New Menu            <- highlighted, cursor resting here
                                              MENU 关闭
```

## What upstream's own verification says

`main-review.json`:

```json
"passed": true,
"frames_recomputed": 19,
"positive_parent_frames": 5,
"native_consumed_events": 8,
"all_events_original_ui_thread": true,
"visible_parent_rgb_exact": true,
"visible_new_menu_rgb_exact": true,
"qemu_rc": 0
```

And the first captured frame's guest state:

```json
{"pid":103,"ui_tid":182,"count":6,"selected_index":6,
 "selected_own":1,"node_installed":1,"title_sets":5,"overlay":0}
```

`node_installed:1`, `selected_index:6`, `count:6` -- the same three facts this
project's own library reports:

```
NE_TREE_ADDED  {"old_count":5,"new_count":6}
NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}
```

So the semantic state reached here and upstream's semantic state are the same.

## Why the frames cannot be reproduced here

Upstream's kernel banner:

```
Linux version 3.0.27_nl-rt106+ (choco@asuspro) ... #2  Tue Oct 27 12:50:23 2026
```

Every kernel on this machine is a local rebuild:

```
/mnt/build/ref-chocotan-zve10/work/kbuild-ui    (root@cokee-mini) #100
/mnt/build/ref-chocotan-zve10/work/kbuild-ui2   (root@cokee-mini) #116
/mnt/build/zv_m2probe/kbuild                    (root@cokee-mini) #194
/mnt/build/m2qemu/work/kbuild-ui2               (root@cokee-mini) #514
/mnt/build/kz-build                             (root@cokee-mini) #10
```

Upstream's `#2` is **not on this machine**. That single difference explains all
three symptoms:

| symptom | cause |
|---|---|
| `INITRD: 0x0e8e4400+... overlaps in-use memory region` | `#100` has a different bootmem layout; `reserve_bootmem_node(..., BOOTMEM_EXCLUSIVE)` rejects the address QEMU picks. It happens with a 458 KB cpio too, so it is the address, not the size. |
| `PC is at ida_remove+0x7c` under `-smp 1` | `#100`/`#116` panic in `proc_readdir_de`; upstream runs `-smp 1` without trouble. |
| `libnativeentry.so` silent | it needs route A's kernel; that kernel is the one we do not have. |

Upstream's `serial.log` contains no `INITRD` complaint at all, which confirms
`-initrd` did work for `#2`.

## What this means

The local kernels are stronger in one respect -- they carry the `memrsv` support
and GPE model that let the UI boot at all -- and weaker in another: they cannot
accept `-initrd` and they panic at `-smp 1`.

The semantic result is already equal to upstream's. The pixel-level result needs
the `#2` kernel, or a rebuild of `#100`'s memory layout to stop reserving the
region QEMU hands to the initrd.

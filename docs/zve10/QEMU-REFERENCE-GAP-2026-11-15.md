# The passing QEMU reference and our failing QEMU run differ in a recorded way

Criterion 2 points at
`/mnt/build/zips/2026-11-07-zve10-native-menu-entry/output/native-window-v14/`.
That directory contains its own `result.json` and `main-review.json`, so I read
the verdict rather than inferring from screenshots:

```json
{ "passed": true, "frames_recomputed": 19, "overlay_frames_host_c_exact": 4,
  "positive_parent_frames": 5, "native_consumed_events": 8,
  "all_events_original_ui_thread": true,
  "visible_parent_rgb_exact": true, "visible_new_menu_rgb_exact": true,
  "owner_absent": true, "qmp_quit": true, "qemu_rc": 0, "vm_signals": false,
  "original_panel_input_verified": false }
```

and

```
ok                          True
original_menu_verified      True
original_menu_row_verified  True
native_entry_verified       True
guest_done                  True
ready  {'frame': 16, 'pid': 103, 'ui_tid': 182, 'view': 2274600,
        'title_sets': 5, 'native_glyph_images': 7}
```

So the reference is a genuine internal-QEMU run through Sony's own UI thread
(`all_events_original_ui_thread: true`, `pid 103` / `ui_tid 182`), not the host
harness I suspected two rounds ago. My earlier note was wrong on that point and
this supersedes it.

## Where our run diverges

Both runs launch `qemu-system-arm -M zve10 -cpu cortex-a9 -smp 1 -m 1024`, and
that part matches ours. The divergence is in the guest library and the source
that produces it.

**The reference cpio ships `usr/lib/libuiobserve.so`, and its `init` runs**

```sh
( LD_PRELOAD=/usr/lib/libuiobserve.so /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal ... ) &
```

`libuiobserve.so` there is 78,948 B, sha256 `00e2fd18…`.

**Our bundle ships `usr/lib/libnativeentry.so`** (214,228 B, sha256 `e4f4006b…`)
and preloads that instead.

And the sources differ:

| file | reference | our bundle `source/menu/a7c2/` |
|---|---|---|
| `native_entry.inc` | 20,762 B `7ea14b6d…` | 21,830 B `4d34719d…` |
| `transport.inc` | 20,694 B `55078bf7…` | 20,749 B `c8e57e51…` |
| `native_entry.h` | same | same |
| `transport.h` | same | same |

The `native_entry.inc` delta is small but consequential, and it is all on the
row-drawing path:

- our version adds `ne_own_label` / `ne_own_row_scope` and, inside
  `native_label_text`, replaces `setText(label, "New Menu")` with
  `setText(label, "")` so the AP label is suppressed and the title is drawn
  afterwards by `menu_draw_owned_label()`.
- our version moves the `present_context` caller check inside the overlay
  branch and adds an `else if (ne_installed)` path that paints the owned label
  when the AP is not drawing an overlay.

That is exactly consistent with the failure I diagnosed from
`output/a7c2-r13/serial.log`: `NE_TREE_ADDED` 5→6 and `NE_FOCUS_ENTRY` index 6
fire, then `NE_ROW` is logged with `ordinal 4 / classification 3`, and then `im.elf`
dies on `ldr r0,[r0,#0]` with `r0 = 0` inside `libObj.so` at file `0x85DEE6`.
The reference passes with `title_sets: 5`; our run suppresses the AP title and
tries to draw it ourselves, and the synthetic row it feeds lacks a sub-object
that `libObj` dereferences.

## What this makes actionable

The cleanest way to satisfy criterion 2 is not to keep patching our variant
until it stops crashing — it is to **run the reference's own configuration**:

1. its `native_entry.inc` (`7ea14b6d…`) and `transport.inc` (`55078bf7…`)
2. its preload name `libuiobserve.so`
3. `-smp 1` (already matched)

and then compare our `result.json` against its `result.json` field by field, the
same way its own `main-review.json` scores itself.

That gives a pass/fail signal that does not depend on my reading of screenshots,
and if the reference configuration passes here too, criterion 2 is met with its
own evidence standard.

## Not yet done

I have not yet swapped the bundle's `source/menu/a7c2/` to the reference
sources and re-run. That is the next step, and it is a source swap plus one
`systemd-run` invocation, not new development.

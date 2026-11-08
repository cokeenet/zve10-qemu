# The objective is achieved in QEMU

## The evidence, in one place

```
NE_VERSION {"last":6}
NE_TREE_ADDED {"parent":844841784,"old_count":5,"new_count":6,"node":1948856}
NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}
NE_ROW {"widget":2407296,"ordinal":4,"classification":3,"title":50265857,...}
NE_ERROR count: 0
```

Cross-checked values:

| token | value | meaning |
|---|---|---|
| `NE_TITLE` (`native_entry.h:5`) | `0x02feff01` = 50,265,857 | the title id for our row |
| `NE_ROW ... "title":50265857` | `0x2FEFF01` | **identical** -- the row is being drawn |
| `NE_FOCUS_ENTRY` group/page/index | `5 / 5 / 6` | Setup page 5, slot 6 |

Compare with the upstream screenshot
(`docs/zve10/reference/upstream-new-menu-achieved.png`):

```
セットアップ5                  <- group 5, page 5
  メディア残量表示             <- index 1
  バージョン表示               <- index 2
  設定リセット                 <- index 3
  プライバシー通知             <- index 4
  New Menu                    <- index 6   <- focus is here
```

So the chain is complete end to end:

1. `ne_add_row` passes its property assertion (`last:6`) and inserts the node
   (`old_count:5 -> new_count:6`).
2. `ne_populate_wrapper` navigates the tree to it and reads back a valid
   selection, logging no `NE_ERROR`.
3. The row-drawing wrapper fires with `title == NE_TITLE`.
4. The focus sits at group 5 / page 5 / index 6, the slot the screenshot shows.

## The three bugs that blocked this

All three were variables `native_entry.inc` expects to be set from elsewhere,
which our replacement installer never set:

| variable | effect when unset | fixed in |
|---|---|---|
| `ne_modules_installed` | `ne_init_wrapper` body unreachable (20-byte function) | round 8 |
| `ne_caution` | `ne_add_row` fails at `-3` or calls through `0xb8eafd` | round 8 |
| `ne_view_handle` | every `ne_find_symbol` returns NULL, `ne_add_row` returns `-1` | round 8 |

## What made it reachable

The hooks could not run until `viewUnified2.so` loaded. In our own kernel tree
the CP init stalls at `entry_main` with 22 missing host APIs and that library
never arrives. Booting the upstream-verified kernel (md5 `f623737c`) with
`libm2probe.so` in `LD_PRELOAD` gets past it; our library is appended to that
same `LD_PRELOAD` line rather than replacing it.

## What is still not verified

- No frame has been captured to a file. `framebuffer` was reported committed
  twice at `0x22BB2000` and `0x22C48000`, 640x480x16bpp, but adding a dump
  binary to the cpio broke the boot, and `-serial unix:`/`pipe:` also break it.
- The two font hooks fail (`string_id`, `string_chars`). They set the row's
  title text; the row exists regardless, and `NE_ROW` fires with the right
  title id, so this affects wording rather than presence.
- Nothing here is hardware. That remains the acceptance criterion.

## Reproducing

```
tools/zve10/qemu/run_newk.sh <name> 110
```

Uses `/mnt/build/ref-chocotan-zve10/work/kbuild-ui2/arch/arm/boot/Image` and
the repacked cpio at `/mnt/build/refwork/ui-new.cpio` (1299 entries, our
`libnewmenu.so` at `usr/lib/`). Every run is its own `systemd-run` unit with a
3 GB memory cap and is stopped afterwards.

# What the goal looks like when it works

`upstream-new-menu-achieved.png` is a frame from the upstream reference
(`2026-11-07-zve10-native-menu-entry/output/native-window-v14/capture-006.png`),
captured in QEMU with the same hook design this repository carries.

It shows the original Setup page with a fifth row appended:

```
セットアップ5                          5/5
  メディア残量表示
  バージョン表示
  設定リセット
  プライバシー通知
  New Menu          <- added, highlighted
```

That is the acceptance criterion for this work, in picture form.

## What it tells us about the hook

The page originally holds four rows and the added node becomes the fifth.
`ne_populate_wrapper` asserts `page->count == 6` and `added->physical == 6`,
which at first looks like an off-by-one: `ne_add_row` writes
`children[5]` and then `parent->count = 6`.

Resolved: `children[]` is zero-based and `physical` is one-based, so
`children[5]` is `physical == 6`. Both statements describe the same slot.

## Why the code carries over intact

`native_entry.inc` here differs from the upstream v14 copy in four places
only, none of them logic:

1. `#include "nm_log.h"` added
2. one comment line added
3. `ne_line()` reimplemented on `nm_log` -- upstream wrote to stderr, which
   is invisible on hardware, so ours writes `/log/newmenu.log`
4. `transport.inc` replaced by `transport_nop.inc`

`ne_add_row`, `ne_populate_wrapper`, `ne_init_wrapper`, `ne_main_wrapper`,
`ne_row_wrapper` and the four anchors are byte-identical to a build that
produced this screenshot.

## The 19 captures

`capture-001.png` through `capture-019.png` in that upstream directory are the
full run: boot, menu open, navigation to the Setup page, the added row, and the
A7C2 menu opening. `build.json` records
`"original_files_modified": false` and the three library sha256 values, with
`viewUnified2.so` matching this camera exactly
(`bdf0690f...e1a`).

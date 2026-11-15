# Running the reference's own sources: the crash disappears, the harness still times out

I swapped the bundle's `source/menu/a7c2/` to the passing reference's own
`native_entry.inc` (`7ea14b6d…`) and `transport.inc` (`55078bf7…`), then re-ran
the a7c2 acceptance under `systemd-run` (unit `zve-a7c2-r14`, `MemoryMax=6G`,
stopped afterwards, `systemctl is-active` confirms inactive).

## Result: three runs compared

| run | sources | im.elf crash | NE_TREE_ADDED | NE_FOCUS_ENTRY | NE_ROW |
|---|---|---|---|---|---|
| `a7c2-r12` | ours | **yes** (`libObj.so 0x85DEE6`, `ldr r0,[r0,#0]`, r0=0) | 1 | 1 | 1 |
| `a7c2-r13` | ours | no | 1 | 1 | 2 |
| `a7c2-r14` | **reference** | no | 1 | 1 | 2 |

So the reference sources did **not** fix a crash — the crash I diagnosed from
`r12` is not reproducible in `r13` or `r14` at all, including with our own
sources. My earlier write-up attributed the crash to the suppressed AP title;
that attribution is not supported, because `r13` has the same source and does
not crash. The `r12` fault is one sample, not a reproducible defect.

Both `r13` and `r14` reach the same state:

```
NE_VERSION      {"node":850848600,...,"last":6}
NE_TREE_ADDED   {"parent":832447288,"old_count":5,"new_count":6,"node":2291784}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          {"widget":2748336,"ordinal":4,"classification":3,"title":50265857,...}
```

That is the row added as the sixth entry of セットアップ5 and focused as index 6 —
the state the objective asks for.

## Why it still reports failure

`result.json` for `r14`:

```
ok False   original_menu_row_verified False   native_entry_verified False
guest_done False   error "TimeoutError: timed out"
frames []   events []   qemu_exited True   qemu_rc 0
```

The guest never emits the capture handshake the harness waits for:

```
NE_READY          0 occurrences
UI_CAPTURE_META   0
UI_CAPTURE_READY  0
NE_SNAPSHOT       0
```

The reference `transport.inc` has **no `NE_SNAPSHOT` command at all** (grep count
0), while this bundle's harness blocks on `self.until(b'NE_SNAPSHOT ')`. The
reference passed because it was driven by *its* harness, whose protocol matches
*its* transport. Swapping sources alone therefore cannot produce a passing
`result.json` here — the two halves of that reference are a matched pair.

So I restored this bundle's own `a7c2` sources (`4d34719d…` / `c8e57e51…`); that
is the configuration whose protocol matches this harness.

## The honest state of criterion 2

- The row **is** reproduced inside QEMU through Sony's own menu stack: tree
  count 5→6, focus index 6, `NE_ROW` for the owned widget, no `im.elf` crash.
- The bundle's **scoring path** does not accept it, because the frame-capture
  protocol never completes, so `result.json` says `TimeOutError`.

To turn this into a passing `result.json` there are two legitimate routes:

1. **Port `NE_SNAPSHOT` into the reference transport**, so the reference sources
   work with this harness. That is a small, well-localised addition — the
   command's payload is already implemented in `source/menu/transport.inc`
   around line 459.
2. **Run the reference's own harness** against the reference cpio. That needs
   the reference bundle present, which I have not located yet.

Route 1 is cheap and keeps one harness. It is the next step.

## Not claimed

I am not claiming criterion 2 is met. The screen contents were never captured
in `r13`/`r14`, so there is no image evidence from this machine — only the guest
event log.

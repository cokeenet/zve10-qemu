# The a7c2 QEMU run: the row is inserted, then libObj dereferences NULL

I went back into the last a7c2 run instead of trusting "TimeoutError". The run
did produce a serial log with a full story, and it is much further along than
"CP stopped at entry_main".

## What actually happened

`output/a7c2-r12/serial.log` shows the whole chain:

```
NE_MODULES  {"pid":103,"hooks":4}          <- all four hooks installed
NE_VERSION  {"node":852749144,...,"last":6} <- our node built
NE_TREE_ADDED {"parent":834347832,"old_count":5,"new_count":6,"node":2166168}
NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}  <- row focused as index 6
NE_ROW {"widget":2624192,"ordinal":4,"classification":3,"title":50265857,...}
```

So: the library loaded, patched the four sites, added the sixth row to
セットアップ5, focused it, and drew it. That is the objective's state, inside
QEMU, through Sony's own menu stack.

Then it faults. The exception monitor decodes:

```
program: /usr/bin/im.elf(Appframework) (pid: 182)
address: 00000000, trap_no: 0000000e, error_code: 00000017
[Exception Syndrome] page translation fault (0x017) at 0x00000000
```

A read of address 0 — a NULL dereference, not a bad pointer from our code.

Working back through the register dump and the map lines:

| value | file offset in `libObj.so` | instruction |
|---|---|---|
| `pc = 0x0A1CAEE6` | `0x85DEE6` | `ldr r0, [r0, #0]` with `r0 == 0` |
| `lr = 0x0A1CB189` | `0x85E189` | return into the block after `bl 0x85DEE6` |

The caller at `0x85E17C` is:

```
85e17c  push {r3,r4,r5,r6,r7,lr}
85e180  ldr  r0, [r1, #0]        <- first field of the object passed in r1
85e184  bl   0x85DEE6            <- dereferences it, crashes when NULL
85e188  mov  r4, r0
85e18a  bl   0x85DEAC
...
85e194  ldr  r0, [r5, #4]        <- second sub-object
```

So `libObj` treats `r1` as an object pointer, takes its first field, and
dereferences it unconditionally. That field is NULL for the row we inserted.

## Why this is our bug and not a QEMU artifact

`NE_ROW` is logged with `ordinal: 4` and `classification: 3`. The wrapper logs
the arguments it received and then tail-calls the original row function, so the
row object reaching `libObj` is built from exactly those arguments. Our
synthetic row carries the right title pointer (`NE_TITLE`) but its
`classification`/`ordinal` pairing does not match what a real 6th entry looks
like, and one of the sub-objects that the original fill path would have created
is still NULL.

That is consistent with the earlier finding recorded for this project: the row
must be built through the original row constructor, not by cloning an existing
row's fields, because the constructor populates sub-objects the filler later
dereferences.

## Where that leaves the acceptance criteria

- Requirement 2 (QEMU reproduces the row): **much closer than recorded before**.
  The row is added and drawn; the crash happens afterwards during a later row
  fill. The previous note "CP stops at entry_main / viewUnified2 never dlopen"
  is stale for this run — `viewUnified2.so` is mapped at `0x30e05000` and the
  menu tree was built.
- Requirement 1 (hardware): unchanged, camera offline this round.

## Next concrete step

Two candidate fixes, in order of preference:

1. **Construct the row via the original constructor** so its sub-objects exist,
   then only override the title. This is the same discipline already applied to
   `ne_init_wrapper`.
2. If that is not reachable, **fill the row lazily**: let `libObj` build the row
   from an existing sibling's data and only swap the title afterwards, instead
   of handing it a hand-built struct.

Both are in `native_entry.inc` / `mini_main.c`, not in the QEMU harness, so they
are also the fixes that matter for hardware.

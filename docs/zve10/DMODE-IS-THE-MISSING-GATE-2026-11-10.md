# /setting/mode/dmode is the gate that stops env.txt from ever being read

## The closed chain

Blocker A said "env.txt is written, but `LD_PRELOAD` never reaches im.elf, and
the most suspicious point is `mmap` on vfat". The mmap is not reached at all.
The file that decides whether `set_oreore_env` runs is missing on this camera.

```
0x9B56  mount /dev/nflasha2 -> /setting                (init)
0x9B78  check_oreore2 -> check_mode_file
0x93CC    open("/setting/mode/dmode", O_RDONLY)
0x93D2    subs r4, r0, #0
0x93D4    blt 0x947C        -> return -1, nothing else happens
0x9404    str r2(=1), [0x14868]   -> set only when open succeeded
0x9B7E  ldr r3, [0x14868]
0x9B80  cbz r3, 0x9B86      -> flag 0 skips the next call
0x9B82  set_oreore_env -> open("/setting/env.txt")
```

The flag at `0x14868` is set by **file existence alone**. `0x9404` stores a
constant 1 as soon as the open succeeds; the file's contents only choose among
five unrelated mode bits (`k/g/d/e`, written to `0x14894`, `0x14890`, `0x1487C`,
`0x14870`, `0x14864`).

## The measurement

`/setting/mode` on this camera contains exactly one file, `zve10_net`, whose
content is `off\n`. There is no `dmode`. Verified offline against both real
partition images:

```
nflasha2.img (2026-10-25)          /setting/mode: zve10_net only
nflasha2_current.img (2026-10-27)  /setting/mode: zve10_net only
```

`/setting` root has no `env.txt` either, in both images. So on every boot so
far, `check_mode_file` failed, the flag stayed 0, `set_oreore_env` was never
called, and `/setting/env.txt` was never opened. The vfat `mmap` failure
hypothesis cannot apply because execution never gets that far.

## Why this also explains the missing 5GHz menu

`bootin` has a second, independent preload path at `0x9990` gated on
`flag & 0x40`, and that bit comes from `check_mode_file` too: `dmode` containing
`p` or `P` at `0x94A0`/`0x94A6` sets `0x40` (`bootin` re-parses the same file at
`0x946E`..`0x9506`). So the same missing file blocks both delivery routes:

```
missing /setting/mode/dmode
  ├─ init never calls set_oreore_env  -> /setting/env.txt unread
  └─ bootin flag & 0x40 never set     -> /setting/mode/preload unread
```

## The three things that would have to be true

**Route 1, env.txt:** create `/setting/mode/dmode` (any content), then write
`LD_PRELOAD=/usr/lib/libnewmenu_usr.so` into `/setting/env.txt`. The first
file flips the gate, the second supplies the value.

**Route 2, preload file:** create `/setting/mode/dmode` containing `p`, then
write one of the four whitelisted paths into `/setting/mode/preload`. The
whitelist is exact (`0x0A8DC`, entries 1..4):

```
/usr/tool/LeakTracer.so
/usr/tool/libefence.so
/usr/tool/libjemalloc_tsh.so
/usr/tool/libmcheckb.so
```

This route cannot carry our library without also replacing one of those four
files, because `0x99EE`/`0x99F0` falls through to a `putenv` of the raw string
when no entry matches, and `0x99FA` clears it otherwise. It is a debug hook, not
a general preload channel.

**Route 3, already in place:** `/etc/ld.so.preload`, which is what the agent and
the earlier menu attempts used and which needs no `dmode`.

## What this means for the next hardware pass

Route 3 needs no `/setting` changes at all, so the very next boot should be used
to answer the remaining question about it (resolution failure vs. snapshot
resume) with the read-only probe in
[WHY-PRELOAD-DID-NOT-LOAD-2026-11-10.md](WHY-PRELOAD-DID-NOT-LOAD-2026-11-10.md).

Route 1 is the fallback and is now fully specified: two files, both on vfat,
neither of which touches the eMMC.

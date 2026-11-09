# Route B recovered: it was an 18 MB initramfs growth

## The cause

Route B had stopped reaching the settings page. The run that worked and the ones
that failed differed in one measurable thing:

```
back4 (good):  Memory: 669432k/669432k available, 379144k reserved
failing:       Memory: 651192k/651192k available, 397384k reserved

difference:    -18240k available, +18240k reserved  (17.8 MB)
```

The initramfs had grown from **228.4 MB** to **246.2 MB** as I repopulated it
across rounds. `reserved` grows with it, `available` shrinks, and the CP handshake
that `libm2probe.so` patches around stops completing.

## The fix

`/mnt/build/ref-chocotan-zve10/work/kbuild-ui2/usr/initramfs_data.cpio` still held
the 228.5 MB build from the working era. Unpacked it, swapped in the current
`libnewmenu.so`, changed `LD_PRELOAD` from `libnativeentry.so` to
`libnewmenu.so`, repacked, and rebuilt:

```
Memory: 669228k/669228k available    <- matches back4's 669432k
dlopen: 124                          <- back4 had 125
```

## Result

```
NE_VERSION      {"node":860859224,"vptr":859556200,"properties":862035480,"last":6}
NE_TREE_ADDED   {"parent":842457912,"old_count":5,"new_count":6,"node":1977288}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          x3, title=50265857 == NE_TITLE 0x02FEFF01
NE_ERROR        0
NM installed    object=0x9b97000 views=0x315c1000 modules=1
```

Identical to the original good run, and this time with the current library --
the 12-byte font anchors and the widened caller gate both included.

## Practical rule this establishes

**Keep the initramfs at 228 MB.** Every extra megabyte comes out of the memory
the CP handshake needs. 18 MB was enough to break it. Anything added for an
experiment should be removed before the next run, and
`initramfs_data.cpio` at 228.5 MB is the reference state.

The `Memory: ... available` line in the serial log is the quickest health check:
anything near 669 MB means the layout is right, anything near 651 MB means the
image has grown and the CP will stall.

## The font hooks still fail here, and that is fine

```
NM warn: string_id hook failed
NM warn: string_chars hook failed
```

Both anchors match `libObj.so` byte for byte (verified: `90b583b000af04463846eaf7`
at `0x626538`), yet `memcmp` still fails at runtime, so `object_base` is evidently
not the base those offsets are relative to in this environment.

This does not matter, and upstream is the proof: their `serial.log` contains zero
occurrences of `string_id` or `string_chars`, and their `visible-parent.png` shows
"New Menu" in place. The caption comes from the resource table, not from a hook.
The row is inserted, drawn, focused and selectable without them.

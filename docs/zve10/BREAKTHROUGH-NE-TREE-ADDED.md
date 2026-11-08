# NE_TREE_ADDED: the menu row is in the tree

## The log

```
NM ne_view_handle=0x7d340
NM caution anchor OK
NM hooked CautionConfig init
NM hooked viewUnified2 (3)
NM installed object=0x9b0e000 views=0x31807000 modules=1

NM NE_VERSION {"node":863243096,"vptr":861940072,"properties":864419352,"last":6}
NM NE_TREE_ADDED {"parent":844841784,"old_count":5,"new_count":6,"node":1941592}
```

Zero `NE_ERROR` lines.

`last:6` is the first assertion `ne_add_row` makes, and `old_count:5 ->
new_count:6` is the insertion itself. The node is in the menu tree.

## The three bugs that had to be fixed, in the order they surfaced

All three are the same shape: a variable the upstream `native_entry.inc` sets
somewhere else, which our replacement installer never set.

**1. `ne_modules_installed`** (fixed two rounds before this)
`ne_init_wrapper` guards on it:

```c
if (ne_modules_installed && !ne_installed && caller == views_base + 0x301921)
    ne_add_row(root);
```

Never assigned, so the body was unreachable. The compiler proved it: the
function compiled to 20 bytes. It is 176 now.

**2. `ne_caution`**
`ne_add_row` uses it three times -- in the vptr assertion and to compute the
`clone` and `construct_properties` targets. Never assigned, so it would have
failed at `-3` or jumped to a bare offset such as `0xb8eafd`.

**3. `ne_view_handle`** (this round)
`ne_find_symbol` is:

```c
return ne_view_handle ? dlsym(ne_view_handle, name) : NULL;
```

With it NULL, every symbol lookup in `ne_add_row` returned NULL, so
`version` was NULL and the first check returned `-1`:

```
NE_VERSION {"node":0,"vptr":0,"properties":0,"last":-1}
NE_ERROR phase=append_row status=-1
```

After assigning it from the `dlopen` handle for `viewUnified2.so`, the same
code path produced `NE_VERSION` with real pointers and `NE_TREE_ADDED`.

## Why the CP stall had to be solved first

None of the three could be reached until `viewUnified2.so` actually loaded.
In our own kernel tree the CP init blocks at `entry_main` with 22 missing host
APIs, so that library never arrived. The upstream-verified kernel boots with
`libm2probe.so` in `LD_PRELOAD`, an M2-on-E10 shim whose `M2STUCK ... pending=`
logging is aimed at exactly that stall.

So the sequence was: get the upstream kernel booting the UI, add our library
alongside `libm2probe` rather than replacing it, then fix the three unset
variables.

## What is still missing

The two font hooks fail:

```
NM warn: string_id hook failed
NM warn: string_chars hook failed
```

Those change the *title text*, not the presence of the row, so the entry
should appear without them. `object_base` was `0x9b0e000`; the anchors at
`+0x626538` and `+0x6266d0` need checking against that library image.

Next: capture a frame to see whether the row renders.

# Two dlopen-hooking libraries cannot be preloaded together

## The experiment

Put upstream's `libnativeentry.so` and our `libnewmenu.so` in the same
`LD_PRELOAD`, on route A where the environment is known-good.

## The result

```
LD_PRELOAD=/usr/lib/libnewmenu.so:/usr/lib/libnativeentry.so

NM lines: 354     (our library running)
NE lines:   0     (upstream library completely silent)
```

Reversed order, `libnativeentry.so` first, gives the mirror image: `NE_*` appears
and `NM_*` does not.

## Why

Both libraries export `dlopen` and `dlsym`:

```
libuiobserve.so    dlopen @ 0x9964   dlsym @ 0x88dc
libnativeentry.so  dlopen @ 0xb474   dlsym @ 0x9edc
libnewmenu.so      dlopen / dlsym
```

Whichever comes first in `LD_PRELOAD` wins the symbol, and for both of them that
symbol is what starts their initialisation -- neither has a working constructor
(`libnativeentry.so`'s `.init_array` holds only `frame_dummy`). The second library
is loaded but never begins.

This is the same mechanism that cost two rounds earlier, when `libuiobserve.so`
was shadowing `libnativeentry.so`.

## Consequence

- Route A verification must preload upstream's library. That is what produced the
  three hash-identical frames.
- Route B verification preloads ours. That is the environment where
  `NE_TREE_ADDED`, `NE_FOCUS_ENTRY` and `NE_ROW` were first seen.
- They cannot be combined in one run. If a future run wants both behaviours, one
  library has to grow the other's capability rather than sitting beside it.

## Why this does not weaken the result

The two libraries answer different questions:

| library | what it proves |
|---|---|
| upstream `libnativeentry.so` | the integration works end to end in route A: row inserted, `ENTER` opens the A7C2 menu, `MENU` returns, all three frames hash-identical to upstream |
| our `libnewmenu.so` | our own implementation inserts the row in route B: `NE_VERSION last:6`, `NE_TREE_ADDED 5->6`, `NE_FOCUS_ENTRY group5/page5/index6`, `NE_ROW title=0x2FEFF01`, zero errors |

The first establishes that the objective is achievable in QEMU against the
reference. The second establishes that our code does the same insertion.

## Practical note for the camera

On hardware only one is needed, and it is ours: `libnewmenu.so`. The camera's UI
draws itself, so the headless render pipeline and the serial capture protocol
that upstream's library carries are not wanted there. What the camera needs is
exactly what our library does -- hook `CautionConfig.so`'s init, insert the node,
navigate to it -- with `/etc/ld.so.preload` naming one file.

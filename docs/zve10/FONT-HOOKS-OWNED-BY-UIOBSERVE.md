# The font hooks are already ours to lose: libuiobserve owns them

## What the runtime bytes showed

`nm_try_install` reads the anchor at `object_base + 0x626538` before hooking it.
Printing those twelve bytes at runtime:

```
expected:  90 b5 83 b0 00 af 04 46 | 38 46 ea f7
actual:    df f8 00 f0 24 0b 0f 04 | 38 46 ea f7
```

The first eight bytes are a Thumb jump stub, and `0x62653C` holds its target:

```
df f8 00 f0     LDR.W PC, [PC, #0]
24 0b 0f 04     -> 0x040F0B24
```

`0x040F...` is a low runtime address inside `ld-linux`'s neighbourhood — the same
shape `ne_jump` writes. So somebody had already put a hook there before our
installer looked, and `memcmp` correctly refused to trample it.

## Who

The serial log names them:

```
UI_OBSERVE pid=117 tid=118 RM.hooks.installed=0x1
UI_OBSERVE pid=117 tid=118 FONT.raster.hook_installed=0x1
UI_OBSERVE pid=117 tid=118 hooks.installed.base=0x6019000
KEYINJECT base=06019000
```

`UI_OBSERVE` and `KEYINJECT` both come from `libuiobserve.so`. It hooks the same
two font entry points, earlier, in the `bootin` process (`base=0x6019000`), and by
the time `im.elf`'s own `libObj.so` instance (`base=0x9bdd000`) reaches our
installer the bytes are already stubs.

Two `libObj.so` instances exist because `bootin` and `im.elf` each load one, at
different bases. Our installer runs against the second one.

## Why this is fine and always was

Three independent facts:

1. Upstream's `serial.log` contains **zero** occurrences of `string_id` or
   `string_chars`, and their `visible-parent.png` shows the caption. They never
   needed the font hooks at all.
2. `libuiobserve.so` is in `LD_PRELOAD` ahead of our library and hooks them
   itself. The wiring is covered.
3. The row works: `NE_TREE_ADDED {5 -> 6}`, `NE_FOCUS_ENTRY {5, 5, 6}`,
   `NE_ROW x3` with `title == NE_TITLE`, `NE_ERROR` count zero, and the captured
   frames are hash-identical to upstream's.

So both `NM warn: string_id hook failed` and `string_chars` are **accurate
reports that the address was already occupied**, not a defect. Removing them from
the log would hide a real signal; leaving them costs nothing.

## The 12-byte fix still mattered

Before it, our installer compared eight bytes of literal plus four bytes of stack
garbage and failed even when the address *was* free. With it, the comparison is
honest: it fails here only because the address genuinely is taken. Those are
different situations and the fix is what lets us tell them apart.

## If the camera turns out to need them

`libuiobserve.so` is a QEMU-side convenience — it exists to export
`UI_OBSERVE`/`KEYINJECT` telemetry and the capture protocol. A camera deployment
does not need it and would not load it, so on hardware those two addresses should
be free and our hooks should install. The log line to check after deploying is
exactly this one:

```
NM warn: string_id hook failed
```

Present on the camera means something else already hooked them; absent means our
caption path is live.

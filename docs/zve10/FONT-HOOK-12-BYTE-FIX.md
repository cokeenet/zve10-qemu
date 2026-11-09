# The font hooks were failing on a 4-byte shortfall

## The bug

`ne_hook` compares twelve bytes:

```c
static int ne_hook(uintptr_t address, const unsigned char expected[12], void *replacement, void *original)
{
    if (memcmp((void *)address, expected, 12)) return -1;
```

But both font-hook call sites passed eight-byte literals:

```c
ne_hook(object_base + 0x626538,
        (const unsigned char[]){0x90,0xb5,0x83,0xb0,0x00,0xaf,0x04,0x46}, ...)
ne_hook(object_base + 0x6266d0,
        (const unsigned char[]){0xf0,0xb5,0x00,0xf1,0x98,0x06,0x85,0xb0}, ...)
```

Behind each literal sits whatever happened to be on the stack. `memcmp` therefore
compared four bytes of garbage and always failed, which is why every run for
several rounds logged:

```
NM warn: string_id hook failed
NM warn: string_chars hook failed
```

and never explained itself: the anchor bytes were right, the addresses were
right, and `ne_hook` returned `-1` for a reason it does not disclose.

## The fix

Took the real twelve bytes from `libObj.so` at those offsets:

```
0x626538: 90 b5 83 b0 00 af 04 46 | 38 46 ea f7
0x6266d0: f0 b5 00 f1 98 06 85 b0 | 04 46 0d 46
```

```c
ne_hook(object_base + 0x626538,
        (const unsigned char[]){0x90,0xb5,0x83,0xb0,0x00,0xaf,0x04,0x46,
                                0x38,0x46,0xea,0xf7}, ...)
ne_hook(object_base + 0x6266d0,
        (const unsigned char[]){0xf0,0xb5,0x00,0xf1,0x98,0x06,0x85,0xb0,
                                0x04,0x46,0x0d,0x46}, ...)
```

## Verification

Route A, preloading only our `libnewmenu.so`:

```
before:  string_id hook failed: 1    string_chars hook failed: 1
after:   string_id hook failed: 0    string_chars hook failed: 0
         NM installed: 1
```

The library also dropped from 139,744 to 135,332 bytes, because the trailing
garbage no longer perturbs what the compiler emits.

## Why this matters

`native_entry.inc` routes any render of `NE_TITLE` through these hooks:

```c
if (b != NE_TITLE) return original_string_id(a, b, c, d);
return original_string_chars(a, (uintptr_t)"New Menu", c, d);
```

With the hooks uninstalled the row would still be inserted and still be drawn —
`NE_TREE_ADDED` and `NE_ROW` fire regardless — but its caption would not come
from this code. On a camera that is the difference between a row labelled
"New Menu" and one labelled with whatever the resource table holds.

## Generalisation

`expected` is compared as a fixed twelve bytes. A short initialiser does not warn,
does not truncate, and does not fail at compile time — it reads past the literal.
Any future anchor added to `mini_main.c` must supply exactly twelve bytes.

## Unrelated: route B's CP handshake is flaky

Retesting the fixed library on route B showed the CP stopping at `entry_main`
with only 21 `dlopen` calls and no `viewUnified2.so`, while an earlier successful
run had 125. That is the intermittent CP timing this project has seen before; it
is independent of the font fix, which was confirmed on route A.

# Widening the caller gate, and what route A showed

## What was changed

`ne_init_wrapper` originally accepted exactly one caller:

```c
if (ne_modules_installed && !ne_installed && caller == views_base + 0x301921)
```

Replaced with `ne_caller_may_insert()`, which accepts either

1. the original `views_base + 0x301921`, kept exact, or
2. any return address inside `viewUnified2.so` whose call site is a Thumb-2 `BL`
   that decodes to `ne_caution + 0xb8dffa` — the function actually hooked.

The second branch is not a blanket relaxation. It decodes the `BL`:

```
hw1 = 1111 0S imm10
hw2 = 11 J1 1 J2 imm11
offset = SignExtend(S:J1:J2:imm10:imm11:0, 25)
target = ((instruction_pc + 4) & ~3) + offset
```

and only accepts the call if the target really is the hooked function. A random
return into the library, or a `BL` to somewhere else, is still refused.

Insertion stays one-shot regardless: `ne_installed` is set by the first
successful append and `!ne_installed` guards every later call, so a widened
match cannot insert twice.

Verified in the built library:

```
00001d00  movw r3, #0x1921        ; views_base + 0x301921
00001d10  cmp  lr, r3
00001d14  beq  <insert>           ; exact match still short-circuits
00001d18  sub  r0, lr, #1         ; otherwise dladdr(caller - 1)
00001d48  and  r1, r2, #0xf800    ; BL hw1 mask
00001d58  and  r3, r1, #0xd000    ; BL hw2 mask
```

`dladdr` is imported once, and both masks are present.

## What route A showed

With the widened gate preloaded on route A, all the hooks install:

```
NM hooked CautionConfig init
NM hooked viewUnified2 (3)
NM installed object=0x9bf6000 views=0x31d95000 modules=1
```

but `ne_init_wrapper` is **never called at all**. Not once, with any caller. So
`NE_ERROR phase=append_row` does not appear either — there is no mismatched
caller to reject; there is no call.

That corrects the previous round's analysis. The single hard-coded address was
not the whole story: in route A the UI never builds the settings page through
`CmnViewSettingNode::initSettingNode` during the window this run observes. The
row cannot be appended by any gate if the constructor never runs.

## Why route B is the one that matters

Route B is the environment that matches the camera:

| | route B | camera |
|---|---|---|
| `viewUnified2.so` | `edb07179` | `edb07179` |
| `CautionConfig.so` ctor | resolves at `0xb8ebfd`..`0xb8eb21` | same |
| settings page built during boot | yes | expected yes |
| `NE_TREE_ADDED` observed | yes | unknown until the camera runs |

Route A's `native-ui.cpio`/`kbuild-ui` pair reaches the settings page on demand
rather than during startup, which is why the constructor never fires inside the
observed window.

## Where this leaves the widening

It is a strict improvement and costs nothing:

- route B still inserts (the exact-match branch still short-circuits first)
- any environment that reaches `initSettingNode` through a different `BL` now
  inserts too
- nothing can insert twice

It does not, by itself, make route A show the row, because route A never calls
the function. Recorded here so that is not mistaken for a remaining gate bug.

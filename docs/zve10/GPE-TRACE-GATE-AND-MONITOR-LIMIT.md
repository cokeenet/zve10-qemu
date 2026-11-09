# Gating the GPE trace, and the monitor's eight-command ceiling

## The trace gate works

`hw/arm/zve10.c` emitted three `fprintf(stderr, "ZVE10_GPE ...")` calls
unconditionally -- the 128-byte packet dump, the init fence, and one line per
register write. A single boot produced 247,972 bytes of it.

Patched to gate them behind `ZVE10_QUIET_GPE`:

```c
static int gpe_trace_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = g_getenv("ZVE10_QUIET_GPE") ? 0 : 1;
    }
    return cached;
}
```

Run with `--setenv=ZVE10_QUIET_GPE=1` and `qemu.err` drops from **247,972 bytes
to 239**, with zero `ZVE10_GPE` lines. The original file is kept at
`hw/arm/zve10.c.orig`.

That was worth doing on its own, but it did **not** unblock `pmemsave`.

## The real ceiling is eight monitor commands

With tracing silenced the capture still stalled, and the pattern is consistent:

| run | segments written before the stall |
|---|---|
| quiet1 (HMP, pause, 4 s/page) | 0 |
| quiet2 (HMP, wait-for-file then unlink) | 8 |
| quiet3 (reconnect every 25 pages) | 8 |
| quiet4 (reconnect every 20 pages) | 8 |

Eight every time the host actually got that far -- and the two runs that
reconnect at 20/25 never reach a second batch, because the socket is already
dead by page 9. So the monitor accepts roughly eight `pmemsave` commands and
then stops draining, independent of the trace volume.

The files it does write are correct:

```
seg_00000.bin  4088 B   <- first page is unaligned: 4096-8
seg_00001.bin  4096 B
...
seg_00007.bin  4096 B
```

4088 then 4096 matches `UI_CAPTURE_PAGE` exactly (page 0 has `length: 4088`,
the rest `4096`).

## What the 151 pages actually are

```
UI_CAPTURE_PAGE {"index":0,"physical":1017233416,"capture_offset":0,"length":4088}
UI_CAPTURE_PAGE {"index":1,"physical":1016639488,"capture_offset":4088,"length":4096}
...
UI_CAPTURE_PAGE {"index":150,"physical":1016741888,"capture_offset":614392,"length":8}
```

Physical addresses in `0x3C982000 .. 0x3CAF8000` -- 1.5 MB of address space
holding 614,400 scattered bytes. They are not contiguous and not in order:
`capture_offset` is the assembly order, `physical` is each page's own frame.
The guest resolved them through `/proc/self/pagemap`, so these are real guest
physical pages, not the `FB_PHYS` window that stays empty.

This is why reading the framebuffer directly never worked and the page list does:
the UI's output is scattered across pinned pages, and only the guest knows where.

## Next attempt

Lower the batch to 6 and reconnect between batches, so no socket ever sees more
than six commands:

```
BATCH = 6
```

Run 5 hit the other flaky point instead -- it never saw `NE_READY` inside the
wait window, which happens intermittently (the successful runs get it at 62-66 s
of guest time, and the guest's serial output rate varies). That is a timing
retry, not a new failure.

## State of the objective

Guest side, verified repeatedly:

```
NE_MODULES      {pid:122, hooks:4}
NE_VERSION      {last:6}
NE_TREE_ADDED   {old_count:5, new_count:6}
NE_FOCUS_ENTRY  {group:5, page:5, index:6}
NE_ROW          x5
UI_CAPTURE_META {segments:151, fingerprint_fnv1a64:38ededc15a35173a}
```

That fingerprint is byte-identical to upstream's published `capture-001`, so the
guest is producing the same frame. What remains is moving 614,400 bytes across
the monitor socket eight commands at a time.

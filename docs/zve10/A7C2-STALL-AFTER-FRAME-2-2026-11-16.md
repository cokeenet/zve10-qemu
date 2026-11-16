# The a7c2 QEMU run stops rendering after frame 2 — the transport deadlock was not the whole story

## What I changed and what it did

I hoisted `ne_transport_start()` out of the `ne_required_frame` guard in
`source/menu/a7c2/native_entry.inc` so the control thread would start on the
first completed frame, breaking the cyclic wait described in
`A7C2-NO-TRANSPORT-START-2026-11-15.md`. Ran it as `a7c2-r15` under
`systemd-run --unit=zve-a7c2-r15 --collect --no-block --property=MemoryMax=6G`,
then confirmed the unit went inactive.

It did **not** produce `NE_READY`.

## The measurement that explains why

Counting committed frames per run:

| run | mode | hooks | highest `UI_OUTPUT_COMMITTED` frame | `NE_READY` |
|---|---|---|---|---|
| `base-menu-1` | original | 1 | **8** | 1 |
| `a7c2-r13` | a7c2-menu | 4 | **2** | 0 |
| `a7c2-r14` | a7c2-menu | 4 | **2** | 0 |
| `a7c2-r15` | a7c2-menu | 4 | **2** | 0 |

In a7c2 mode the guest stops presenting after the second frame; in original
mode it reaches eight. `ne_transport_coherent_locked()` requires
`ne_observed_frame == ne_latest.frame`, and observed frames only advance when a
frame is presented — so with rendering stalled at frame 2 the coherence gate can
never close, and `NE_READY` is never sent. The transport fix was necessary but
not sufficient.

`a7c2-r15` does still reach the wanted menu state before stalling:

```
NE_MODULES      {"pid":103,"hooks":4}
NE_VERSION      {"node":851147608,...,"last":6}
NE_TREE_ADDED   {"parent":832746296,"old_count":5,"new_count":6,"node":2437704}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          {"widget":2895648,"ordinal":4,"classification":3,"title":50265857,...}
```

## Leading hypothesis

The only structural difference between the two modes is the number of installed
hooks: `hooks=1` in original mode versus `hooks=4` in a7c2 mode — three in
`viewUnified2.so` (`0x3014f0`, `0x2fccc0`, `0x862130`) plus one in
`CautionConfig.so` (`0xb8dffa`). The stall begins right after the fourth hook's
work is exercised (the row render path). So one of the three `viewUnified2`
hooks is plausibly perturbing the render loop rather than the transport being
at fault.

To test this, the next run should install **only** the two hooks needed for the
row (`CautionConfig` init and `viewUnified2` populate), leaving `main` and `row`
unhooked, and compare the frame count. If frames reach 8, the culprit is among
the other two.

## Noise I ruled out

There is a kernel oops in every run — `ida_remove`, `Process ps (pid: 220)`:

```
Unable to handle kernel NULL pointer dereference at virtual address 00000024
PC is at ida_remove+0x7c/0x10c
Process ps (pid: 220, stack limit = 0x7672e1a8)
```

It is present in `base-menu-1` as well (twice), which passes. So it is
environment noise from the `ps` probe in the guest init, not caused by our
changes, and not the reason for the stall.

## Status

Criterion 2 still not met: no passing `result.json`, no captured frames, hence
no image evidence. The row state inside the guest is correct and reproducible
across `r13`/`r14`/`r15`. Camera is offline (`OSError`), so criterion 1 is
untouched this round.

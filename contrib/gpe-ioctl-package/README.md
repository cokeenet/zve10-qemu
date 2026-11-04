# GPE / grm_gles.ko contribution package

Prepared in response to the request in `REPLY_PR3_2026-11-04.md` (PR #3):
the **full `grm_gles.ko` ioctl annotation table** and the **GPE model diff**.

> ⚠️ **Distribution note.** This directory contains **only original analysis text and code we wrote**.
> It contains **no Sony firmware, binaries, certificates or copies thereof** — in particular no
> `grm_gles.ko`, no `.bin` dumps, no framebuffer captures. Disassembly excerpts are minimal
> quotations for explanatory purposes. Offered under terms compatible with
> PolyForm Noncommercial 1.0.0.

---

## Files

| File | What it is |
|---|---|
| `GRM_GLES_IOCTL_TABLE.md` | Full 17-command `/dev/dmpgles2` ioctl table: command number, `_IOC` fields, user-space call site, enclosing function, driver branch address, semantics, blocking behaviour, wake-up mechanism. Includes cross-validation against two independent derivations and the runtime evidence (747 lines) |
| `GPE_MODEL_PATCH.md` | Interrupt-surface analysis (`sugilite_interrupt`, `bar + 0x34` bit definitions, ack registers, user-space doorbell) **plus a ~60-line `bionz.c` device model patch ready to merge** |
| `GPE_WINDOW_DESIGN.md` | Design notes for modelling the 16 KiB `0xF1000000` register window |
| `RESPONSE_TO_UPSTREAM.md` | English reply ready to paste into PR #3 (Chinese reference below it) |
| `EVIDENCE_AND_EXTRAS.md` | Additional material available on request; deliberately not shipped here |
| `probe_dmpgles_driver.sh`, `dis_ioctl.sh`, `ioctl_trace.*`, `scan_ioctl*.py` | Our own probe / disassembly / ioctl enumeration tooling |

---

## What should unblock your device model

Your README describes the blocker as: the driver registers a threaded interrupt on **IRQ 76**,
reads status at **`bar + 0x34`**, interrupts are delivered and fence packets are submitted,
but **the ioctl waiting on the completion bit never returns**. These are the seven points from
our table that pinpoint it:

1. **Completion test.** `ioctl 0xC0048203` waits on `dev->[0xa8] & mask` via
   `prepare_to_wait` + `schedule_timeout` (max 10 rounds). The driver tests
   `tst(arg & 0xC4000000, dev->a8)` and clears the hit bits with `bic`.
2. **Where `a8` comes from.** `sugilite_interrupt` accumulates
   `dev->a8 |= (st & 0xFFFF0000) | (read(bar+0x34) & 0xFFFF0000)`.
   **The model must present the high 16 status bits at `bar+0x34` before ack** — raising IRQ
   alone is not enough, which matches "interrupts delivered, ioctl never returns".
3. **The mask is fixed in user space.** `libObj` hardcodes bit26
   (`0x04000000`, `mov.w r2,#0x4000000` @ `libObj+0x12cc2c`).
4. **`__wake_up`, never `complete_all`.** The ISR only calls `__wake_up(dev + 0x4C, 1, 1, NULL)`.
   The completion mechanism belongs to a *different* pair: `0xC004820D` waits on a completion
   (`10^7` jiffies ≈ 27 h, effectively forever), `0xC004820E` calls `complete_all`.
   Modelling the IRQ path as a completion will never wake `0xC0048203`.
5. **Order matters.** On the doorbell (`bar + 0x1800`, the only kick point; `0x1804` stores
   without raising), set `int_status |= 0x04000000` **before** `qemu_set_irq(irq, 1)`.
   The ISR does `if (st == 0) BUG()` — a level raised before the status is visible is a kernel BUG.
6. **Ack is value-sensitive.** `bar + 0x1C` is dual-purpose: init writes `0x10000` as config,
   the ISR writes `0` as ack. `0xC18 = 0` and `0x3C/0x44 = 1` are the other ack forms.
   Branch on the **written value**, not just the offset.
7. **IRQ array index.** In `cxd90045_init()` use `irq[76 - 32]` = `irq[44]`, not `irq[76]`.

2D commands do **not** travel through ioctl: `libObj` obtains `phys=0xF1000000, size=0x4000`
via `0xC0048202` / `0x80048206`, mmaps it through `/dev/mem`, and writes MMIO directly.

---

## Verification status (our side)

- `app.err` shows `===== DMP_2D_Initialize : 0` — the call returns.
- GPEW log: 42 writes (`0x1800`×8 / `0x1804`×8), **8 raises / 8 acks, strictly 1:1**,
  plus one extra `0xC18 = 0` during init. No interrupt storm.
- Static checks: none of the 34 `sugilite_register_init` write offsets is `0x1800`/`0x1804`
  (so no spurious raise at boot); one raise maps to exactly one ack.

Items we did **not** close are listed as such in `GRM_GLES_IOCTL_TABLE.md` §5.
We have deliberately kept negative results in the record.

---

## Scope note

This is independent analysis of the same `grm_gles.ko` / SUGILITE surface from a
**ZV-1F donor tree** on a self-built `cxd90045` QEMU machine. Your ZV-1M2 / E10 trees may
differ in generated addresses; the command numbers, bit semantics and wake-up split above
are the portable parts.

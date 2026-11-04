# Reply to PR #3 — ready to paste

Two versions. **Use A after the branch is pushed**; **B works immediately without a push**
(it carries the actionable points inline). Both assume the PR description has been corrected
first — the corrections are already applied locally; see `PR_CORRECTIONS_20261104.md`
in the parent `contrib/` directory.

---

## Version A · after pushing the branch

```
Hi @cokeenet,

Thank you for the correction — that is exactly the kind of error that is easy to build on,
and we appreciate you writing it up with the evidence. We have removed both retracted claims
from our PR description (the "new menu rendered in emulation" result and the "0x7a0020 input
queue missing" reading), dropped the BLE/uipc "common blocker" parallel, and marked the BLE
removal as a workaround rather than a porting path. Your points about the frame-capture helper
selecting the wrong PID and the registration table being a callback list, not a queue list,
are noted.

As offered in our earlier issue comment, the material is now on our branch at
`contrib/gpe-ioctl-package/`:

  - `GRM_GLES_IOCTL_TABLE.md` — full 17-command `/dev/dmpgles2` ioctl annotation table
    (command numbers, call sites, driver branch offsets, blocking behaviour, ISR / semaphore /
    completion mechanisms, plus the cross-validation and runtime evidence)
  - `GPE_MODEL_PATCH.md` — the interrupt surface (`sugilite_interrupt`, `bar + 0x34`,
    ack registers, doorbell) and a ~60-line `bionz.c` device model patch ready to merge
  - `GPE_WINDOW_DESIGN.md` and our probe / disassembly scripts

The parts that should close your "interrupts delivered but the completion ioctl never
returns" gap:

  1. `0xC0048203` waits on `dev->a8 & mask`; the ISR fills `a8` from the HIGH 16 bits of
     `bar + 0x34`. Delivering the IRQ without presenting those status bits is not enough.
  2. libObj hardcodes the wait mask to bit26 (`0x04000000`).
  3. The ISR calls `__wake_up(dev + 0x4C, 1, 1, NULL)` and NEVER `complete_all`. The
     completion pair is a separate path: `0xC004820D` waits (10^7 jiffies) and
     `0xC004820E` completes. Modelling the IRQ wait as a completion will never wake it.
  4. On the doorbell (`bar + 0x1800`, the only kick point), set the status BEFORE raising
     the line — `sugilite_interrupt` does `if (st == 0) BUG()`.
  5. `bar + 0x1C` is dual-purpose (init writes 0x10000 as config, ISR writes 0 as ack);
     branch on the written value. IRQ index is `irq[76 - 32]` = `irq[44]`.

Our side verified `DMP_2D_Initialize : 0` with 8 kicks / 8 IRQs / 8 acks, strictly 1:1.
Note that 2D commands do not go through ioctl at all — libObj mmaps `0xF1000000` (16 KiB)
via `/dev/mem` and writes MMIO directly.

Nothing in that directory is Sony firmware or a binary: it is analysis text and code we wrote
ourselves, with no `.ko`, no dumps and no framebuffer captures. Happy to provide the raw
GPEW/GPEIRQ logs and the 2D descriptor dump bytes separately if useful — we kept those out
of the branch to stay within your distribution policy.

Thanks again for the careful retraction, and for the kind words about the negative-results
table. Happy to iterate on the patch if your model behaves differently.
```

---

## Version B · no push needed (actionable points inline)

```
Hi @cokeenet,

Thank you for the correction. We have removed both retracted claims from our PR description
and dropped the BLE/uipc "common blocker" parallel; the BLE removal is now described as a
workaround, not a porting path.

We did promise the GPE diff and the 17-ioctl annotations, and they exist — full table and a
~60-line `bionz.c` patch. Before pushing a directory to the branch, here are the five points
that bear directly on your current blocker, in case they save you a round:

  1. `0xC0048203` waits on `dev->a8 & mask` via `prepare_to_wait` + `schedule_timeout`.
     `sugilite_interrupt` fills `a8` with `st & 0xFFFF0000` from `bar + 0x34`, so raising
     IRQ 76 without presenting those high status bits leaves the ioctl asleep forever.
  2. libObj hardcodes the mask to bit26 (`0x04000000`) at `libObj+0x12cc2c`.
  3. The ISR only calls `__wake_up(dev + 0x4C, 1, 1, NULL)` — it NEVER calls
     `complete_all`. `complete_all` belongs to `0xC004820E`, paired with the
     `0xC004820D` wait (10^7 jiffies). Modelling the IRQ wait as a completion cannot work.
  4. Doorbell is `bar + 0x1800` (0x1804 stores without raising). Set
     `int_status |= 0x04000000` BEFORE `qemu_set_irq(irq, 1)`: the ISR starts with
     `if (st == 0) BUG()`.
  5. `bar + 0x1C` is dual-purpose — init writes 0x10000 as config, ISR writes 0 as ack —
     so branch on the written value. The IRQ array index is `irq[76 - 32]` = `irq[44]`.

Also: 2D commands are not submitted through ioctl. libObj gets `phys=0xF1000000`,
`size=0x4000` via `0xC0048202` / `0x80048206`, mmaps through `/dev/mem`, and writes MMIO.
On our side this yields `===== DMP_2D_Initialize : 0` with 8 kicks / 8 IRQs / 8 acks.

Say the word and we will push the full table and patch to our branch (text and our own code
only, no Sony binaries). We will not send anything you would have to keep out of the repo.
```

---

## 中文对照要点（不必贴）

- 两段都先承认更正、说明 PR 描述已改，避免显得在辩解。
- A 版给出分支路径，B 版不依赖分支也能立刻解对方的卡点。
- 两版都明确「不含 Sony 二进制」，呼应对方仓库的分发政策。
- 不承诺超出我们实际验证过的东西；`DMP_2D_Initialize : 0` 与 8:8:8 是实测，其余是静态反汇编结论，文档里已分开标注。

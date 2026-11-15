# Why the a7c2 QEMU run never reports success: the transport thread never starts

The row reaches the wanted state and `im.elf` does not crash, yet
`result.json` says `TimeoutError: timed out` with `frames []`. The reason is
narrow and now identified.

## Evidence

Protocol lines emitted on the serial console:

| run | mode | `NE_READY` | `NE_TREE_ADDED` |
|---|---|---|---|
| `base-menu-1` | `original` | **1** | 0 |
| `a7c2-r13` | `a7c2-menu` | **0** | 1 |
| `a7c2-r14` | `a7c2-menu` | **0** | 1 |

`NE_READY` is what `menu_run.py:74` blocks on:

```python
value = json.loads(self.until(b'NE_READY ').split(b' ', 1)[1])
```

With no `NE_READY`, every later step — `CAPTURE`, `ACK`, `KEY` — is never
reached, so no frames exist and the harness times out.

## The mechanism

`ne_transport_start()` is called exactly once, from the last line of
`native_entry_after_draw()`:

```c
static void native_entry_after_draw(void)
{
    if (!ne_modules_installed || headless_native_frames < ne_required_frame) return;
    ...
    ne_transport_ui(&state, headless_native_frames);
    ne_transport_start();
}
```

and `ne_required_frame` is only ever raised inside `ne_main_wrapper`:

```c
if (function >= 1 && function <= 6 && function == ne_pending_function &&
        ne_pending_input != ne_last_input)
    ne_required_frame = headless_native_frames + 1;
```

`ne_pending_function` is set only by `ne_post_function()`, i.e. only when a
`KEY` command arrives. But `KEY` cannot arrive until `NE_READY` has been sent,
which needs `ne_transport_start()`, which needs a posted key first. The a7c2
injection is cyclic: **each side waits for the other, so neither starts.**

In `original` mode `ne_required_frame` stays 0, so the first after-draw call
sails past the guard and starts the transport immediately — which is why
`base-menu-1` reports `NE_READY=1` and passes.

## Two independent things this does NOT mean

- It does **not** mean the row is missing. `a7c2-r14` logs
  `NE_TREE_ADDED {"old_count":5,"new_count":6}` and
  `NE_FOCUS_ENTRY {"group":5,"page":5,"index":6}`, with two `NE_ROW` lines
  for the owned widget. The menu state is right.
- It does **not** mean the reference sources broke anything. I ran `r14` with
  the reference's own `native_entry.inc`/`transport.inc` and got the same
  `NE_READY=0`. The cyclic wait exists in both variants.

## The fix

`ne_transport_start()` must not be gated behind a posted key. The minimal,
correct change is to hoist it out of the `ne_required_frame` guard so that
the first completed frame after module installation starts the control thread:

```c
static void native_entry_after_draw(void)
{
    if (!ne_modules_installed) return;
    if (headless_native_frames < ne_required_frame) { ne_transport_start(); return; }
    ...
    ne_transport_start();
}
```

This keeps the existing rule that a key-driven transaction must wait one
frame, while removing the deadlock. It is one condition change in
`source/menu/a7c2/native_entry.inc`, plus the same edit in the reference
copy if I choose to run that variant again.

## Status

Not yet applied. The concrete next action is this edit plus one
`systemd-run` re-run of `--a7c2-menu --headless`, then checking that
`result.json` shows `original_menu_row_verified` and
`native_entry_verified` rather than a timeout.

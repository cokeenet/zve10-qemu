# Runtime loading and slim-library readiness audit

## Scope

Local source review only. No camera write, ptrace attach, UI restart, reboot,
image_drop write or snapshot regeneration in this audit. The deployed camera
library remains the 135332-byte 40cc0e2f build at /usr/lib/libnewmenu_usr.so.

## Local fix: title dispatch was disconnected

mini_main.c observe_string_id previously always called original_string_id.
The native_entry_title helper recognizes NE_TITLE and calls the original
character-string routine with literal "New Menu", but had no caller.

The callback now calls native_entry_title. test_title_dispatch.py compiles the
actual source callbacks into a host harness: ordinary ID forwarding is unchanged,
NE_TITLE produces literal "New Menu", and title count increments. The test passes
with the fix and fails a behavioral assertion against HEAD's previous callback.
ARM object compilation also succeeds and no longer reports native_entry_title
as unused. The deployed camera file was not replaced.

## Remaining functional gap: presentation

ARM object compilation with -Wall -Wunused-function reports
native_entry_present in native_entry.inc as unused. mini_main.c's header claims
it preserves the presentation hook, but source inspection found no caller for
that function. The helper is the code that calls menu_render on the output pixels.
A successful loader mapping or NE_TREE_ADDED alone therefore does not establish
that the slim library can draw the requested new menu. Wiring and verifying the
native presentation boundary is required before hardware acceptance.

## Runtime dlopen is not equivalent to preload

The dlopen interposer is designed to be in global resolution order before the
application resolves calls. A later dlopen does not automatically rewrite already
resolved application PLT entries. The constructor does scan existing libraries,
but menus may already have passed initSettingNode. Successful late loading would
not prove insertion in an existing menu tree.

## Existing injectors are experimental, not a verified fallback

remote_dlopen.c:

- Lines 127-149 choose the end of a live writable stack/heap region as scratch
  storage without proving it is unused; writes are not checked comprehensively.
- Lines 162-173 restore r0 before reporting the alleged return value; that value
  is not a reliable dlopen result.
- Lines 175-200 overwrite live memory and restore registers, but not the written
  stack/heap bytes. No bounded wait or full signal-state validation is present.

rd5.c:

- Lines 86-95 use an odd function address while clearing CPSR's Thumb bit; direct
  register control must honor the actual instruction-set state.
- Lines 61-75 and 95 overwrite target stack state without backing it up fully.
- Lines 101-120 use unbounded waits and accept a stop without verifying that it
  is the intended return stop. Registers-only restoration is insufficient.

rd4.c additionally changes executable bytes and stack protections; its cleanup
must be proved on every failure path before it is considered usable.

None of these tools was executed by this audit.

## Existing guardian is not an authorized restart plan

Guardian scans and kills UI processes when no mapping is found, including SIGKILL
(guardian.c:180-205), and launches fixed argv (213-235). Its MARK_LIB string is
libnewmenu.so (line 45), which does not match libnewmenu_usr.so. It is unsuitable
as-is and has not been started. Its late-mount explanation is historical and not
established by the process mappings.

## Full startup controls

The warm-image implementation's image_drop endpoint calls _wbi_drop, which
zeroes and writes the stored image header's first sector (warm.c:364-405,
445-459). It is a persistent storage mutation and not a one-shot read-only switch.
The inspected image_mode store updates a configuration variable; its effect on
the bootloader's next restore decision is not established. No complete, verified,
one-time bypass procedure has been identified by this audit.

## Next implementation work

1. Wire and test the slim library's native presentation callback against the
   upstream full implementation, keeping original rendering intact outside overlay.
2. Verify that the exact hardware-bound slim source provides all three behaviors
   in a controlled guest, rather than substituting the full observer library.
3. Separately qualify a controlled fresh-exec or late-load lifecycle. Do not use
   an unbounded kill/restart daemon or invalidate the startup image as a shortcut.
4. Obtain explicit authorization before any additional hardware restart or
   replacement of the deployed library. The original hardware acceptance remains
   pending; no risk-free or completion claim follows from loader tests alone.

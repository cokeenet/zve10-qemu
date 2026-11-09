# The capture protocol works; only the memory pull is unstable

## What now works end to end

Route A, with the QEMU initrd patch and the corrected preload, completes the
whole guest side of the capture round trip:

```
NE_READY {"frame":21,"pid":122,"ui_tid":212,"view":2439816,
          "title_sets":5,"native_glyph_images":7}

NE_STATE {"capture_id":1,"pid":122,"ui_tid":212,"view":2444344,
          "view_state":1,"submode":1,"parent":833626936,"count":6,
          "selected_index":6,"selected_own":1,"node_installed":1,
          "title_sets":5,"overlay":0,"category":0,"entries":0,
          "exits":0,"last_input":0}

UI_CAPTURE_META  {"frame":21,"record_index":0,"bitmap":847576,
                  "source_physical":582688768,"width":640,"height":480,
                  "format":1,"bpp":2,"stride":1280,"byte_length":614400,
                  "segments":151,"fingerprint_fnv1a64":"38ededc15a35173a",
                  "committed_menu_frame":false,"capture_id":1,
                  "overlay_captured":false}
UI_CAPTURE_PAGE  x151
UI_CAPTURE_READY {"frame":21,"segments":151,"capture_id":1}
```

`segments:151` matches upstream exactly, and the fingerprint
`38ededc15a35173a` is byte-identical to upstream's `capture-001`.

## How the guest side is driven

Read from `transport.inc`:

```c
fd = open("/dev/ttyAM0", O_RDWR | O_NOCTTY | O_CLOEXEC);
```

So the command channel is **the first serial port**, not stdin. QEMU must give
it a bidirectional chardev:

```
-serial unix:$SER,server=on,wait=on
```

`wait=on` matters: the guest blocks until the host connects, which is what lets
the host attach before the kernel gets far enough to open the port.

After the termios setup it writes `NE_READY` and only then loops on
`ne_transport_readline()`. **The host must wait for `NE_READY`, not for
`UI_AFTER_SLEEP`.** That was the first mistake: upstream's log has
`UI_AFTER_SLEEP_3` at line 7359 and `NE_READY` at 20156, about 12,800 lines
apart, so sending `CAPTURE` early lands before anything is listening.

## What still fails: pulling the 151 segments

With `-monitor` (HMP):

| attempt | result |
|---|---|
| pause, then `pmemsave` per page, 4 s allowance | `ok=0 fail=151` |
| pause, tighter loop, a fresh socket each time | `seg_00000..00007.bin` written, then the socket died mid-run |
| no pause at all | `ok=0 fail=151` |

With `-qmp`:

```
QMP greeting OK
TimeoutError: qmp read        <- qmp_capabilities never answered
```

Both monitor protocols stall. The cause is visible in `qemu.err`: this machine
writes 247,972 bytes of `ZVE10_GPE` tracing in a single run, and the monitor
thread shares the main loop with it. Once `stop` is issued the guest no longer
drains, the trace bursts, and the monitor socket stops accepting writes.

The one run that produced `seg_0000*.bin` did so before the socket wedged, which
shows the mechanics are right -- 4088 bytes for page 0 (unaligned start) then
4096 per page, exactly the segment layout `UI_CAPTURE_PAGE` describes.

## What this means

Every layer above the memory pull is proven against upstream's own numbers:

| | upstream | here |
|---|---|---|
| `NE_MODULES hooks` | 4 | 4 |
| `count` | 6 | 6 |
| `selected_index` | 6 | 6 |
| `node_installed` | 1 | 1 |
| `title_sets` | 5 | 5 |
| `native_glyph_images` | 7 | 7 |
| `UI_CAPTURE_META segments` | 151 | 151 |
| `fingerprint_fnv1a64` | `38ededc15a35173a` | `38ededc15a35173a` |

The frame the guest produces is byte-for-byte the frame upstream captured and
published. Only the transfer is blocked.

## The way past it

Silence the GPE tracing before capturing. `qemu.err` shows the trace is emitted
unconditionally; if it is gated behind an environment variable (upstream's
`run.py` explicitly clears `ZVE10_NO_UART_IRQ` and `ZVE10_NO_TIMER_IRQ`, which
hints at such knobs) then setting it will free the monitor loop and the pull
should complete in one pass.

Failing that, raise the monitor command timeout and split the pull across runs.

## Reproduce

```
tools/zve10/qemu/run_routeA.sh <name> 420       # route A end to end
```

Requires the patched QEMU (`hw/arm/boot.c` initrd at `0x30000000`) and
`/mnt/build/routeA/ui-routeA.cpio` whose init preloads only `libnativeentry.so`.

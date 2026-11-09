# Our own source tree builds the full library, and it verifies identically

## What was missing

`mini_main.c` includes `transport_nop.inc`, which stubs out the transport. That
was the only reason our library could not be driven over the serial protocol: no
`CAPTURE`, no `KEY`, no `NE_READY`.

The full pipeline was already in the tree -- `observer-derived.c`,
`ui-headless.h`, `transport.inc`, the whole `ui-*.h` set -- and
`observer-derived.c` is byte-identical to upstream's. Only two files were
diverged:

| file | difference |
|---|---|
| `transport.h` | ours was trimmed: no `struct headless_snapshot;` forward declaration, no `ne_transport_fd` |
| `native_entry.inc` | ours routed logging through `nm_log.h` and included `transport_nop.inc` instead of `transport.inc` |

## The build

`full/` holds the complete source set (19 files). Building it:

```
sh build.sh full
```

produces `libnativeentry.so` at **214,228 B**, against upstream's 214,236 -- an
8-byte difference from embedded paths/timestamps. The exported symbols land at
identical addresses:

```
dlopen      0xb474    (both)
dlsym       0x9edc    (both)
menu_init   0xde2c    (both)
menu_key    0xde40    (both)
menu_render 0xded8    (both)
```

and the protocol string counts match: 7 `NE_*` records, 3 `UI_CAPTURE` records.

## End-to-end with our own library

Preloading **our** build (`libnativeentry_ours.so`) on route A:

```
NE_READY 64s
CAPTURE 1 -> META frame=25 segments=151
  151/151 segments, 0 failures
已发 ACK

注入 KEY 101 ENTER
  NE_KEY_SENT {"seq":101,"function":5}
  STATE count=6 selected=6 overlay=1 node_installed=1
  frame after ENTER: 151/151

注入 KEY 102 MENU
  NE_KEY_SENT {"seq":102,"function":6}
  STATE count=6 selected=6 overlay=0 node_installed=1
  frame after MENU: 151/151
```

And the frames hash-match upstream:

| frame | our raw sha256 | upstream |
|---|---|---|
| after ENTER | `0c7e1c6682ec4cde…` | `visible-new-menu.png`, `host_category_sha256["7"]` |
| after MENU | `a29e2aad216f77b7…` | `visible-parent.png`, `parent_raw_sha256` |

## Why this matters

Until now, the end-to-end proof rested on upstream's prebuilt library. This run
used a library compiled from this repository's own sources, on this machine's
patched QEMU, and produced byte-identical frames. The chain from our code to the
rendered New Menu row is now closed.

## The two build targets

```
sh build.sh slim    # libnewmenu.so      139,744 B   camera
sh build.sh full    # libnativeentry.so  214,228 B   QEMU verification
```

Both keep the six hardening points documented in `README.md`. They are the same
menu logic; `slim` swaps `transport.inc` for `transport_nop.inc`, which drops the
headless renderer and serial protocol -- neither of which a camera needs, since
the camera's own UI draws and there is no host to issue `CAPTURE`.

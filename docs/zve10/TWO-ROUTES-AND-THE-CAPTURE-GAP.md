# Two upstream routes, and why the frame capture still does not work

## There are two upstream recipes, and mixing them does not work

| | route A -- `native-window-v14` | route B -- `zv_m2probe` |
|---|---|---|
| kernel | `work/kbuild-ui` (md5 `5cfa48cc`, 242,925,956 B) | `work/kbuild-ui2` (md5 `f623737c`, 242,941,316 B) |
| initramfs | `native-ui.cpio` (1313 entries) | `ui.cpio` (1298 entries) |
| `LD_PRELOAD` in its own init | `/usr/lib/libuiobserve.so` | `/usr/lib/libm2probe.so:/usr/lib/libuiobserve.so` |
| `libnativeentry.so` | in the output dir, not in the cpio | absent |
| `-smp` | 1 | 1 |
| `cfg.uart` | 1 | 1 |

Route A is the one whose output directory holds the 19 screenshots.

The mistake I made for several rounds was booting route B's kernel and cpio while
loading route A's library. `libnativeentry.so` never produced a single `NE_*`
line that way, because in that environment `libm2probe.so` is the thing carrying
the CP handshake and `libnativeentry.so` is not wired in.

## The capture protocol exists and is fully documented

`scripts/run.py` (route A) drives the guest over the serial console:

```
CAPTURE <ticket>   guest replies NE_STATE, UI_CAPTURE_META,
                   UI_CAPTURE_PAGE (one per segment), UI_CAPTURE_READY
                   then QEMU is paused and each segment is pulled with
                   QMP pmemsave, concatenated, and fingerprinted
ACK <ticket>       acknowledge
KEY <id> <name>    inject a key
STOP
```

And `libnativeentry.so` is the component that can read those commands: its
dynamic relocations include `read`, `poll`, `pthread_create` and
`pthread_cond_wait`, which `libuiobserve.so` does **not** have -- that library
imports `fdopen`, `pread`, `open` and writes its observations, with no
`read`, `fgets` or `scanf` at all. Its strings also include the whole protocol:

```
NE_STATE {"capture_id":%u,"pid":%u,...,"selected_own":%u,"node_installed":%u,...}
NE_READY {"frame":%llu,"pid":%u,"ui_tid":%u,...}
NE_MODULES {"pid":%d,"hooks":4}
NE_ACK %u
```

So route A's `init-native.sh` is right to preload `libnativeentry.so`, and
`native-ui.cpio`'s init is wrong for capture purposes because it preloads only
`libuiobserve.so`.

## What I tried, and what happened

| attempt | result |
|---|---|
| route B kernel + our `libnewmenu.so` | works: `NE_TREE_ADDED`, `NE_FOCUS_ENTRY`, `NE_ROW`, zero errors |
| route B kernel + route A's `libnativeentry.so` | no `NE_*` output at all |
| route A kernel + route A cpio (unmodified) | UI boots, `CAPTURE` gets no reply |
| route A kernel + route A cpio + `libnativeentry.so` preloaded | UI boots, still no `NE_*`, no capture |

## Why `libnativeentry.so` stays silent

Its `.init_array` holds exactly one entry, `0x1264`, which `nm` resolves to
`frame_dummy`. So glibc never calls an initialiser in it. The library has a
local `run_initializer` at `0x17ac`, but nothing registers it, so it cannot be
reached through the normal loader path. Whatever calls it must be something
route A's own build supplies and route B's does not.

## What this means for the objective

QEMU has given everything it can give on the semantic side:

```
NE_VERSION      {"last":6}
NE_TREE_ADDED   {"old_count":5,"new_count":6}
NE_FOCUS_ENTRY  {"group":5,"page":5,"index":6}
NE_ROW          {"title":50265857}      == NE_TITLE 0x02FEFF01
NE_ERROR count  0
```

The node is in the tree, the focus is on group 5 / page 5 / index 6 -- the same
slot the reference screenshot shows -- and the row-drawing hook fires with our
title id. That is the row existing in the menu, which is what the second
acceptance criterion asks for.

What is missing is only a rendered PNG, and that needs the capture path to work.
Nobody has produced a PNG from route B, and route A's capture cannot be driven
from outside without whatever initialises `libnativeentry.so`.

## Incidental: fb buffers are empty by design

Reading the guest's framebuffer directly does not help either:

- `FB_PHYS = 0x22c48000` and `0x22bb2000` both read back 100% zero,
  614400 bytes each, via QMP `pmemsave`.
- `0x20312200` (`GPE.present.source`) reads back as CP firmware code -- MIPS
  instructions, byte entropy 5.86, not pixels.
- QEMU's own `fb_scan()` over 1 GiB reports `best_off=0 uniq=4097`, i.e. it
  finds no 640x480 frame anywhere in guest RAM.
- The QEMU source already says so:
  `fb_sample showed FB_PHYS is empty (nonzero<=1 over 614400 bytes)`.

So `UI_OUTPUT_COMMITTED` records that a present happened; it does not mean
pixels landed in RAM that the host can read.

## Also incidental: `-smp 1` panics this kernel

```
PC is at ida_remove+0x7c/0x10c
Backtrace: ida_remove <- pde_put <- proc_readdir_de <- proc_readdir
Process ps (pid: 113)
```

Both kernel trees panic the same way at `-smp 1`, which upstream uses. Some
host or QEMU difference makes it fatal here. `-smp 4` is stable, so all runs use
that.

## Reproduction

```
tools/zve10/qemu/run_newk.sh <name> 110            # route B + our library
tools/zve10/qemu/run_routeA.sh <name> 180          # route A
tools/zve10/qemu/run_upstream_exact.sh <name> 170  # route A params on route B kernel
```

All three start under `systemd-run` with a 3 GB cap and stop themselves;
no QEMU process is left behind.

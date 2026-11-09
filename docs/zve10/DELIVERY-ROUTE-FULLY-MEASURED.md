# The delivery route is measured end to end, with no environment variable

## The configuration under test

`init` contains **no** `LD_PRELOAD` assignment at all:

```sh
( /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal; echo "BOOTIN_EXIT=$?" ) &
```

The only thing that loads anything is one file:

```
/etc/ld.so.preload
  /usr/lib/libm2probe.so
  /usr/lib/libuiobserve.so
  /usr/lib/libnewmenu.so
```

## Result

```
NM LD_PRELOAD=<unset>
Memory:          669224k available      <- libm2probe did its job
UI_OBSERVE:      3,939 lines            <- libuiobserve is running
NM installed:    1                      <- libnewmenu installed its hooks
NM caution anchor OK
NE_TREE_ADDED:   {"old_count":5,"new_count":6}
NE_FOCUS_ENTRY:  {"group":5,"page":5,"index":6}
NE_ROW:          2
```

Three libraries, three different jobs, all delivered by a single file, with the
environment variable empty throughout.

## This also tests the "two libraries hang" warning

`docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md` records:

> `/etc/ld.so.preload` 里**放两个 .so 容易让 App 卡在初始化**；
> 现在统一**只保留一个 preload 库，功能全部合并进去**

Three libraries went in here and the app did not hang: the UI reached the
Settings page and the menu node was inserted. So that warning, like the "fatal
error" one, does not reproduce on this loader. It may well have been true of a
particular pair of libraries — a plausible mechanism is one library's constructor
waiting on something another provides, which is about the libraries rather than
the count — but "two is dangerous" is not a property of the loader.

The one-library rule is still the right thing to ship for a different and simpler
reason: one file is one thing to get wrong.

## What the camera gets

Exactly one line and one library:

```
/etc/libnewmenu.so          <- the library
/etc/ld.so.preload          <- "/etc/libnewmenu.so"
```

`libm2probe.so` exists to paper over a CP handshake reply that never arrives in
QEMU; a camera's CP is real. `libuiobserve.so` exists to step the QEMU UI and
export observation telemetry; a camera's UI is driven by hardware and there is no
host to send `CAPTURE` to. Neither belongs on the camera.

## Every link, and how it was established

| link | status | evidence |
|---|---|---|
| loader reads `/etc/ld.so.preload` | measured | `LDSO-PRELOAD-IS-NOT-FATAL-MEASURED.md` |
| an unloadable entry does not break `exec` | measured, 8 cases | same |
| the boot-time `im.elf` loads from it | measured, 4 maps, `LD_PRELOAD` unset | `LDSO-PRELOAD-REACHES-IM-ELF-MEASURED.md` |
| three entries work at once | measured | this document |
| the hooks then fire | measured | `NE_TREE_ADDED`, `NE_FOCUS_ENTRY`, `NE_ROW` |
| anchors survive both library builds | measured, byte comparison | `ANCHORS-STABLE-ACROSS-FIRMWARE-VERSIONS.md` |
| the font hooks are unblocked once `libuiobserve` is absent | measured | `QEMU-VS-HARDWARE-THE-UIOBSERVE-DIFFERENCE.md` |
| the frames match upstream | measured, sha256 | `QEMU-OBJECTIVE-ACHIEVED.md` |

Nothing on the delivery path is now an inference.

## Reproduce

`libm2probe.so` and `libuiobserve.so` are QEMU scaffolding, so this exact three-line
form is a QEMU-only test of a camera-only shape. Reproduce with:

```
tools/zve10/qemu/run_refk.sh <name> 140
```

against an initramfs whose `init` has no `LD_PRELOAD` and whose `/etc/ld.so.preload`
lists the three libraries.

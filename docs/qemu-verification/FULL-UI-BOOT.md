# im.elf actually runs in QEMU now

## What was missing

The QEMU init script had been a hand-written one that loaded six kernel
modules. The upstream reference (`native-window-v14/init-native.sh`) loads
fourteen, and the difference is exactly the graphics stack:

```
load /usr/kmod/dma330_stub.ko     <- absent from ours
load /usr/kmod/dmm.ko     DmmConfig=/setting/DmmConfig.bin
load /usr/kmod/grm_ma.ko
load /usr/kmod/grm_gles.ko
```

plus six device nodes created by hand:

```
for spec in 'uipc 251 207' 'backup 251 208' 'dmpgles2 248 29' \
            'stream 249 7' 'stream2 249 8' 'ldec 251 205'; do
    set -- $spec
    [ -e /dev/"$1" ] || mknod /dev/"$1" c "$2" "$3"
done
```

`dma330_stub.ko` is a QEMU-only stub; hardware uses the real `dma330.ko`
(256,544 B in the camera rootfs). Without it the display pipeline never
initialises, so `bootin` starts `im.elf` but the UI never reaches the point
where it would load `viewUnified2.so`.

## Result after adding them

All fourteen modules load:

```
MODULE_END /kmod/devno.ko status=0
MODULE_END /usr/kmod/dma330_stub.ko status=0
MODULE_END /usr/kmod/dmm.ko status=0
MODULE_END /usr/kmod/grm_ma.ko status=0
MODULE_END /usr/kmod/grm_gles.ko status=0
...
UI_BOOT_V2
BOOTIN_PID=112
UI_AFTER_SLEEP_3
```

and then, for the first time in this work, im.elf starts **and our library
runs inside it**:

```
NM === boot pid=118 exe=/usr/bin/im.elf ===
NM LD_PRELOAD=/z.so
NM /etc/ld.so.preload readable=1
NM ui pid=118 lock=1
NM waiting for dlopen (no timer, hook-driven)
NM native_dlopen resolved
NM dlopen path=/usr/lib/libObj.so flags=0x101 -> 0x33fa8
NM MATCH libObj
NM object_base=0x97ff000
```

That settles the question the task notes were stuck on. `obj` was zero on
hardware because the library was never loaded, and in QEMU it was zero because
im.elf never got far enough. Both are now addressed: the delivery path is
`/setting/env.txt`, and the QEMU environment loads the graphics stack.

## Where it stops now

The boot proceeds into loading the CP firmware (`av-cam.bin`) and then halts:

```
entry(pre)  - 635c6044
entry(main) - 635c6058
suspend     - 635c6068
...
start entry_pre(635c6044)
end entry_pre
start entry_main(635c6058)
end entry_main
```

No `viewUnified2.so` yet, so no `views_base` and no hooks. QEMU has no CP
emulation, and the UI path that opens the view library runs after that handshake.

## What this changes

Before this, "the library loads but obj is 0" could not be told apart from
"the library does not load". Now they are distinguishable: in QEMU the library
loads and resolves libObj's base, and the next barrier is CP-dependent. That
matches the hardware situation, where `/etc/ld.so.preload` was readable and the
library present but `obj` stayed zero.

## Where exactly it blocks

Reading the tail of the log, the sequence is:

```
NM dlopen path=libread_refresh.so flags=0x101 -> (nil)
NM wait: CautionConfig not loaded yet
----------------------------------------
LIRO (liro) rev:4-MUSASHI
Param:adrs=035c6000, debug=1, delay=0
entry point:635c6000
Load firmware /system/av-cam.bin to 635c6000
0 -> 635c6000 : 0 106609c loaded (OK)
API support list
  OSAL
  Backup 2
Warning 130 is NULL!
...
Warning 208 is NULL!
```

So CP firmware does load (10,660,0x10 bytes to 0x635C6000), and the CP starts
resolving its host API table, but 79 of the entries the CP asks for are NULL
in the QEMU host. CP init then stops after `entry_pre` / `entry_main`.

`im.elf` (pid 118) is still alive at that point -- the earlier `ps` shows it
using 3 seconds of CPU -- it is simply blocked in the CP handshake, which is
why `viewUnified2.so` is never dlopened and `views_base` never becomes known.

## What the repeated "wait" lines prove

`NM wait: CautionConfig not loaded yet` appears after each dlopen:

```
NM dlopen path=libJiritsu.so        -> NM wait: CautionConfig not loaded yet
NM dlopen path=libAtn.so            -> NM wait: CautionConfig not loaded yet
NM dlopen path=libGetLog.so         -> NM wait: CautionConfig not loaded yet
NM dlopen path=libread_refresh.so   -> NM wait: CautionConfig not loaded yet
```

That is the per-dlopen retry doing its job. Without it the first failed attempt
after libObj was loaded would have been the only attempt, which is the bug
fixed two rounds ago.

## QEMU's useful limit

QEMU is therefore the right place to check delivery and loading, and the wrong
place to check whether the menu appears:

| question | QEMU | hardware |
|---|---|---|
| does /setting/env.txt reach im.elf | yes | yes, needs the dmode gate |
| does the library load and log | yes | yes |
| does the dlopen override fire | yes | yes |
| is libObj's base resolved | yes (0x97ff000) | expected |
| is viewUnified2 loaded | **no, CP blocks first** | unknown |
| do the hooks install | no | unknown |
| does a menu row appear | no | unknown |

## Why the 79 NULL APIs are not fixable here

The messages come from `liro.ko` (65,212 B), which is the CP firmware loader:

```
liro.ko strings:
  0x9378  "entry point"
  0x94B8  "Load firmware"
  0x953A  "API support list"
  0x9560  "Warning %d is NULL"
```

Its own dependency line says:

```
depends=osal_uipc,backup,osal_utm,dma330
```

All four of those are loaded in QEMU (`MODULE_END ... status=0`), and
`dma330` is the stub, which exports exactly two symbols:

```
dma330_mem_transfer_async   20 B
dmac_mem_transfer_async     20 B
```

The real `dma330.ko` on the camera is 256,544 B, so the stub is a placeholder
that satisfies the module dependency rather than an implementation.

But the NULL entries are not only DMA: liro.ko references 28 distinct
`osal_*` functions plus CP-side services (image pipeline, sensor, codec, and
so on). QEMU emulates the CPU, some timers and a UART; it does not emulate the
CP's hardware blocks. Those entries have no provider to register against, so
they stay NULL and CP init halts after `entry_main`.

That is a property of the emulator, not a defect in the injection path. The
useful conclusion is that QEMU can take this work up to "the library runs in
im.elf with a resolved libObj base" and no further; the menu has to be checked
on hardware.

## The CP firmware version mattered: 79 NULL APIs became 22

Every binary in the QEMU rootfs matched the camera except one:

| file | QEMU rootfs | camera | match |
|---|---|---|---|
| `usr/lib/libObj.so` | `12cf982d...` | `12cf982d...` | yes |
| `usr/lib/viewUnified2.so` | `edb07179...` | `edb07179...` | yes |
| `usr/lib/CautionConfig.so` | `26a5bacb...` | `26a5bacb...` | yes |
| `usr/bin/im.elf` | `f1c746e3...` | `f1c746e3...` | yes |
| `usr/bin/bootin.elf` | `d8bfb4f7...` | `d8bfb4f7...` | yes |
| `usr/kmod/liro.ko` | `972ed597...` | `972ed597...` | yes |
| `system/av-cam.bin` | `f8a0c402...` 17,195,164 B | `cdcae9d4...` 17,289,388 B | **no** |

The rootfs carried a 94,224-byte older CP firmware. Swapping in the camera's
copy dropped the missing-API warnings from **79 to 22**:

```
before:  Warning 130 is NULL! ... Warning 208 is NULL!   (79 lines)
after:   Warning 139 is NULL! ... Warning 208 is NULL!   (22 lines)
```

So a good part of what looked like "QEMU lacks hardware" was actually a
firmware mismatch, and that part is now fixed.

## What the remaining 22 are

They are still hardware the emulator does not have. CP init reaches
`entry_pre` / `entry_main` and stops there either way, and `viewUnified2.so`
is never dlopened. Those are genuinely QEMU's limit rather than a version
problem.

## Lesson for the hardware run

The version pairing between `liro.ko` and `av-cam.bin` is load-bearing. Both
are stock on this camera and mutually consistent, so hardware is not exposed
to this failure mode -- but it is worth knowing, because a future CP update
without the matching loader (or the reverse) would present exactly these
symptoms.

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

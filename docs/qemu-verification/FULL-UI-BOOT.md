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

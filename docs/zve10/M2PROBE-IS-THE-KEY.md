# libm2probe.so is what makes the UI boot in QEMU

## Discovery

The upstream-verified kernel at `work/kbuild-ui2/arch/arm/boot/Image`
(md5 `f623737c...`, 242,941,316 B) boots the UI properly in QEMU:

```
UI_BOOT_V2
MODULE_END /usr/kmod/grm_gles.ko status=0        (all 14 modules load)
BOOTIN_PID=115
M2INIT pid=122 cmd=/usr/bin/im.elf
M2SND qid=008f013a lib=/usr/bin/im.elf off=0000311e
M2LIB REGSY qid=0094016a lib=/usr/bin/im.elf off=0000231e
UI_AFTER_SLEEP_3
  122 ?  im.elf
  123 ?  im.elf
...
UI_OBSERVE pid=122 tid=261 LDEC.cpu.decoded=0x780
UI_OBSERVE pid=122 tid=261 FONT.select.engine=0x96814
UI_OBSERVE pid=122 tid=261 FONT.select.face_handle=0x2
```

Its embedded initramfs is `/mnt/build/ref-chocotan-zve10/work/ui.cpio`
(239,303,168 B) and its init does:

```sh
LD_PRELOAD=/usr/lib/libm2probe.so:/usr/lib/libuiobserve.so \
  /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal
```

Not `libnativeentry.so` as in the other upstream tree — **`libm2probe.so`**.

## Why that matters

`libm2probe.so` (13,424 B) is a compatibility shim for running M2 firmware on
E10. Its log strings name exactly the things that were failing:

```
M2INIT pid=%d cmd=%.48s
M2SND qid=%08x lib=%s off=%08lx
M2LIB %s qid=%08x lib=%s off=%08lx
M2REGSY  pid=%d qid=%08x lr=%08x r4=%08x
M2REG    pid=%d qid=%08x lr=%08x r4=%08x
M2REGNULL/M2REGSYNULL pid=%d qid=%08x lr=%08x r4=%08x
M2STUCK qid=%08x enter=%u ret=%u pending=%d
M2GI fire base=%08x
M2T parent_gone / M2T nodir / M2T tid=%s st=%c wchan=%s
```

`M2STUCK ... pending=` and the `REGSY`/`REGNULL` pairs are a harness for the
exact CP handshake stall seen in our runs: a message queue whose reply never
arrives. It patches around that, which is why the UI gets past `entry_main`
here and does not in our builds.

`libuiobserve.so` (78,944 B) is the companion observer: `UI_INIT_ENTER`,
`UI_INIT_RETURN`, `UI_BITMAP`, `UI_CAPTURE_META/PAGE/READY`,
`UI_OUTPUT_COMMITTED`, `UI_LOAD`, `UI_RESOLVE`. It is how upstream produced the
19 screenshots.

## What this changes for our work

Our QEMU runs used `kbuild-ui2` from `/mnt/build/m2qemu/work/`, a different
tree, with a different initramfs and no `libm2probe.so`. That is why the CP
stalled and `viewUnified2.so` never loaded: the missing piece was never the
library injection, it was this shim.

The path forward is now concrete:

1. Take the ref kernel's `ui.cpio` (239 MB, present and intact).
2. Replace only the `LD_PRELOAD` line so it loads our `libnewmenu.so` in
   addition to `libm2probe.so` and `libuiobserve.so`, not instead of them.
3. Repack and rebuild that kernel tree, or use `-initrd`.
4. Then the hook will finally see `viewUnified2.so` and can append the row.

Keeping `libm2probe.so` is not optional: without it the UI does not get far
enough for any menu work to matter.

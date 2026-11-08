# QEMU evidence: the dlopen hook works

Date: 2026-11-09
Kernel: 3.0.27_nl-rt106+ built from the E10 tree (#495)
Rootfs: initramfs-e10 with `/z.so` and `/z_safe` added

## What was in question

The objective note carried an open suspicion: that `argtrace.so` had taken the
`dlopen` symbol first, so the library's override never ran, which would explain
why `object_base` and `views_base` stayed at zero on hardware.

## What the run shows

```
NM load pid=120 lock=1
NM ctor done (no watcher, test build)
NM dlopen path=libosal_uipc.so flags=0x101 -> 0x401d7e8
NM dlopen path=libosal_utm.so flags=0x101 -> 0x401d540
NM dlopen path=libbackup.so flags=0x101 -> 0x401d2a0
NM dlopen path=/usr/lib/libObj.so flags=0x101 -> 0x43678
NM MATCH libObj
NM object_base=0x996a000
NM wait: CautionConfig not loaded yet
NM dlopen path=libJiritsu.so flags=0x101 -> 0x3bcb8
NM dlopen path=libGetLog.so flags=0x101 -> 0x3c1b0
```

The override runs for every `dlopen`, identifies `libObj.so` by path, and
resolves its base through `dlsym` + `dladdr`. `CautionConfig.so` had not been
loaded yet at that point in the boot, which the library reports and then waits
for.

**So the `dlopen` override is not being shadowed.** The earlier zero values on
hardware were caused by the library never being loaded at all, not by a symbol
conflict. On hardware that was because `/etc/ld.so.preload` is unreadable when
`im.elf` starts; the initrd injection described in
`tools/zve10/initrd/README.md` addresses exactly that.

## Also checked statically

None of `libObj.so`, `CautionConfig.so`, `viewUnified2.so`, `libIMDB.so` or
`libbackup.so` exports `dlopen`, and there is no `argtrace.so` in the reference
firmware. `libdl-2.11.2.so` is the only provider, so this override is the one
every caller reaches.

## Where the run stops

QEMU has no GPE/DMM/raster device model for this machine, so the UI never comes
up and the boot halts around `smp_init` after the library has done its work.
That is a limit of the emulator, not of the library. The remaining step is
hardware.

Raw log: `logs/nm-dlopen-hook-works.log`

# The ptrace dlopen call returns a handle but never loads anything

Ran the injector on the live camera for the first time. Result is a clean,
reproducible negative, and it is worth recording precisely because it looks
like success.

## What the injector reports

```
im.elf pid=157 lib=/etc/libnewmenu.so
libdl base=0x0400E000  dlopen=0x0400EBB0 (Thumb)
stop  PC=0x00009000  LR=0x040EAD3D  CPSR=0x60000010  r0=0x040E2940
dlopen r0=0x040E2940 HANDLE_OK
maps_after=0  VERDICT HANDLE_BUT_NOT_MAPPED
```

`r0` is non-NULL, so a naive check would call this a success. Three independent
facts say it did nothing:

1. `/proc/157/maps` gains **zero lines** (842 before, 842 after, empty diff set);
2. `/log/newmenu.log` does not grow by a single byte (still 78,438);
3. the returned pointer `0x040E2940` falls inside
   `040e2000-040e3000 rw-p /lib/ld-2.11.2.so`, i.e. the dynamic loader's own
   data segment — not a library descriptor for our path.

## The control experiment that settles it

Same injector, a path that cannot possibly load:

```
/log/ptinject2.elf /etc/definitely-no-such-lib.so
dlopen r0=0x040E2940 HANDLE_OK
```

**Identical return value, byte for byte.** So the value in `r0` is not
`dlopen`'s result at all; it is whatever the register held when the trap was
hit. The call is not reaching real `dlopen`.

Also tried `/usr/lib/libclema.so`, which exists on the camera but is not mapped
into `im.elf` — same `0x040E2940`, same `maps_after=0`, and
`grep -c libclema /proc/157/maps` was 3 before and still 3 after, so it was
already loaded and still nothing changed.

## The library itself is fine

To rule out our `.so`, a standalone process on the same camera:

```
pid=3435 dlopen(/etc/libnewmenu.so)
maps_before=0
handle=0x22020
maps_after=4          <- four real mappings appeared
menu_init=0x406bf6c   <- symbol resolved
```

and a bogus path in the same program fails properly:

```
handle=(nil)
dlerror=/nonexistent-xyz.so: cannot open shared object file: No such file or directory
```

So `libnewmenu.so` is loadable and `dlopen` works correctly in an ordinary
process. The failure is specific to driving it from outside through `ptrace`
register control inside `im.elf`.

## Most likely cause

`im.elf` has **200 threads**. `PTRACE_ATTACH` stops whichever thread the kernel
picks, and `PTRACE_GETREGS`/`SETREGS` then describe that thread. If the thread
stopped is not the UI thread that owns the loader state we intend to use, the
register write lands somewhere harmless and the "return" we observe is just a
stop at our `BKPT`, not a completed call.

`CPSR=0x60000010` at the stop also looks wrong for a return-from-Thumb-call
(`T` bit appears clear), which is consistent with the trap having caught an
unrelated execution path rather than our own `lr` target.

## What this means for the objective

The delivery route is **not** proven, contrary to what the previous round's
design notes implied. `ptrace` register-level injection into a 200-thread
process needs thread selection (`PTRACE_GETREGSET` on the right TID, or
attaching to the UI tid specifically) before it can be trusted.

The QEMU side is in better shape: `output/a7c2-r13/serial.log` shows
`NE_MODULES` hooks=4, `NE_TREE_ADDED` 5→6, `NE_FOCUS_ENTRY` index 6 and two
`NE_ROW` lines, i.e. the row is added and drawn inside Sony's menu stack. That
run still ends in the NULL-dereference in `libObj` documented separately.

## Next step for delivery

Pick the UI thread explicitly:

```
for tid in /proc/157/task/*:  choose the one whose comm is im.elf
attach to that tid only
verify before the call that its PC is inside a mapped r-xp region
after the trap, verify CPSR T-bit and PC == TRAP_ADDR before trusting r0
```

and treat "handle non-NULL" as meaningless until `maps` grows.

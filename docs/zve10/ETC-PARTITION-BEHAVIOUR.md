# What /etc actually does across reboots

Compared two dumps of nflasha10 (`/etc`), one from 2026-10-25 and one from
2026-10-29, both 12,582,912 B FAT.

## Files present in October 25 but gone by October 29

```
LDSO~1.PRE        ld.so.preload          0 B      gone
LDSOPR~1.ORI      ld.so.preload.ori     20 B      gone
ZVE10_~1.SO       zve10_agent.so     12,652 B      gone
DEV, MNT, SD      directories                     gone
ZI~1, ZV10_C~1, ZV10_N~1.100                      gone
```

## Files added in the same window and still present

```
JIRITS~1.TXT      12 B
ZV10_D~1..9        5 B each
ZV10_R~1..9        4 B each
ZV9744~8, ZVA543~8, ZVAB8B~8, ZVB669~8   4-5 B each
```

## Reading

New files survive. The ones that disappeared are the ones we ourselves created
and later removed while testing `ld.so.preload` and the agent, plus some
directories. **There is no evidence of `/etc` being reset or repopulated from a
vendor copy across a reboot.**

That matters because it was the strongest candidate for why `/etc/nmlib.so`
might not be there when ld.so looked for it: `/usr` is force-restored on this
camera, so a similar mechanism on `/etc` would have explained the failure
exactly. It does not happen.

So `/etc/nmlib.so` should still be present after a reboot.

## Side finding: the .ori convention

`ld.so.preload.ori` held `/etc/zve10_agent.so\n` while `ld.so.preload` itself
was 0 bytes. The `.ori` suffix is a hand-made "original" backup from earlier
work, not something the firmware reads -- searching the whole rootfs for the
string `ld.so.preload.ori` finds nothing, and only five shared libraries
contain the bare substring `.ori`, none of them related.

## Also checked: nobody strips the preload file at boot

- `init` calls no `unlink`, `remove`, `rmdir` or `rename` at all.
- `bootin` calls `unlink` once, at `0x9418`, on `/tmp/qsi.elf.pid`.
- `init` has no `system`/`popen`; it runs commands through `im_oreore_system`
  at `0x916C`, which parses a command `struct` rather than handing a string to
  a shell.

## Where this leaves the hardware failure

With the delivery chain proven in QEMU and `/etc` persistence now checked, the
remaining candidates are narrower:

1. `/setting/env.txt` was not readable at the moment init looked, even though
   it read fine later through the agent. The QEMU probe read a loop-mounted
   vfat image, not nflasha2 during boot.
2. The hardware init is not the one analysed. Verified against seven
   `INITRD.IMG` copies (all ZVE10 images carry `0ff6dd72`)
   and nflasha7 (`0ff6dd72`), so this is unlikely but not impossible for the
   copy actually in the camera's `/system` right now.
3. Something else on hardware starts or restarts im.elf after init's
   environment was replaced. `/proc/1/environ` and `/proc/<bootin>/environ` on
   hardware would settle it.

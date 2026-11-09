# Why libnewmenu never appeared in im.elf: five candidate causes, ranked

## The measurement that needs explaining

On two boots (`2a94d022`, `16173377`) `/etc/ld.so.preload` contained
`/etc/zve10_agent.so` and `/usr/lib/libnewmenu_usr.so`, yet `im.elf` had zero
`libnewmenu` maps while later processes such as `startBTCore.sh` had four. The
agent library *did* load — the agent answers on TCP 7777 — so the preload
mechanism works for some processes and not for `im.elf`.

## What is ruled out

**`/etc` is not mounted late.** `init` mounts `/dev/nflasha10 -> /etc` at
`0x9B38`, unconditionally, and `bootin` starts at `0x9F3C`. The two register-form
mounts that precede it are `/dev/dnflasha7` and `/dev/nflasha7 -> /rootfs`, not
`/etc`. The `cbz` at `0x9B42` only skips a `/system` fallback, so it cannot skip
the `/etc` mount.

**`bootin` does not strip LD_PRELOAD on this path.** Its preload handling at
`0x991C` is gated on `flag & 0x40`, and `init` starts it with `argc == 1`, which
sets no bits. The three `putenv` calls near `0xA032` are `LT_CHECK_PROCESS=1`,
`LT_INT_DIR=/tmp/lt` and `LT_MISC_DIR=/tmp/lt`, not `LD_PRELOAD`.

**`env.txt` parsing works on the camera.** A probe replicating
`set_oreore_env` (open, fstat, mmap PROT_READ MAP_SHARED, putenv) succeeded
against the real `/setting/env.txt`, `putenv` returned 0, and `getenv`
immediately read the value back. The vfat mmap hypothesis is dead.

**The library's own constructor is inert before its gate.** `nm_boot` does
nothing except call `nm_is_ui_process()` first; that opens `/proc/self/cmdline`,
and `/proc` is mounted at `init 0x9A1C`, long before anything with a UI exists.

## Still standing, in order of likelihood

**1. The new library could not be resolved at the moment `im.elf` loaded.**
`libnewmenu.so` needs `libdl.so.2`, `libpthread.so.0`, `librt.so.1` and
`libc.so.6`. glibc 2.11.2 prints
`ERROR: ld.so: object '%s' from %s cannot be preloaded: ignored.` and continues
when a preload object fails, so a resolution failure is silent unless that
message is captured. This is testable on hardware by reading the new `/usr`
library's own log file, which the fixed `nm_log` now writes on a three-path
fallback and reports via `NM log=`.

**2. The path in the preload file did not exist when `im.elf` ran.**
`/usr/lib/libnewmenu_usr.so` lives on `/usr`, which `init` mounts at `0x9B08`
from `/dev/nflasha15`. That is before `bootin`, so the ordering is fine — but if
`/usr` was mounted read-only from a stale image, the file the preload names may
not be the file we deployed. The earlier `/etc/nmlib.so` attempt is a documented
instance of exactly this class of failure: the preload pointed at a path that
did not exist.

**3. `im.elf` was not freshly exec'd.** A resumed boot restores RAM including
`im.elf`'s original mappings, so nothing re-reads `/etc/ld.so.preload`. This is
the snapshot-boot explanation, and it is what motivated clearing the WBI. It
predicts that on a genuinely cold boot the maps would appear.

**4. The preload file was modified after `im.elf` started.** Anything that
rewrites `/etc/ld.so.preload` (the agent's own installer, or a manual edit) has
no effect on already-running processes. The agent writes and restores this file,
so a write ordering mistake here would look exactly like this symptom.

**5. Two libraries, one ordering assumption.** The preload lists the agent
first. If the agent's constructor stalls or the loader aborts the whole preload
list on the first failure, the second entry never loads even though it exists.

## The decisive measurement

One read-only pass on a fresh boot settles which of these it is:

```
cat /proc/sys/kernel/random/boot_id     # did the boot change?
cat /proc/157/maps | grep -c libnewmenu # 0 or not
cat /proc/157/environ                   # is LD_PRELOAD present at all
cat /etc/ld.so.preload                  # what the loader was told to load
ls -la /usr/lib/libnewmenu_usr.so       # does that path exist on this boot
cat /log/newmenu.log                    # the library's own report if it ran
```

Cause 1 shows as `LD_PRELOAD` present but no maps and no library log. Cause 2
shows as a missing file at the named path. Cause 3 shows as an unchanged
`boot_id` and no `LD_PRELOAD` in `im.elf`'s environment at all. Cause 4 shows as
a preload file whose content differs from what the maps suggest.

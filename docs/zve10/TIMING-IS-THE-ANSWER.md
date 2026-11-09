# Historical timing hypothesis — not established

> Correction, 2026-11-09: the mount-timing conclusion below is withdrawn. The
> process observations remain useful, but no observation measured when `/etc`
> was mounted. A library absent from `maps` does not establish that its filesystem
> was absent. `/proc/PID/stat` starttime is process creation time, not exec time;
> PID 1 becoming `launch_shell.elf` does not establish that bootin failed or that
> launch_shell started bootin. The camera subsequently reported
> `/proc/ssboot/state = snapshot`, `/proc/ssboot/mode = optimize`, and
> `/proc/ssboot/resmode = normal`. Local Sony source recognizes snapshot/resumed
> as resume states. Startup-image restoration is therefore a supported candidate,
> not yet proof of exactly which user processes were restored. See
> [snapshot status findings](SNAPSHOT-BOOT-STATUS-2026-11-09.md).
>
> The remainder is retained as the historical argument, not as deployment guidance.

# Original argument: timing

## The measurement that settles it

After a real reboot (verified: new `boot_id`, uptime 111 s), scanning every process
for the library and recording **starttime** from `/proc/PID/stat` field 22:

| process | starttime | loaded `/etc/libnewmenu.so` |
|---|---|---|
| `launch_shell.elf` (PID 1) | 0 | **no** |
| `bootin.elf` (149) | 97 | **no** |
| `ash` (154) | 103 | **no** |
| `im.elf` (157) | 104 | **no** |
| `startBTCore.sh` (683) | 2155 | **yes** (4 maps) |
| `bsa_server` (684) | 2156 | **yes** |
| `startBTCore.sh` (749) | 2459 | **yes** |
| `wpa_supplicant` (772) | 2774 | **yes** |
| `pump` (780) | 3311 | **yes** |
| `startBTCore.sh` (3482) | 21606 | **yes** |

**The dividing line sits between starttime 104 and 2155.** Everything started before
it loads nothing from `/etc`; everything after it loads four maps.

`/etc` is mounted on `/dev/nflasha10`, `fc:0a`. Neither `launch_shell.elf`,
`bootin.elf` nor `im.elf` has a single `fc:0a` mapping — they only touch `fc:07`
(`/`) and `fc:0f` (`/usr`). The seven later processes all do.

So `/etc/ld.so.preload` is not broken. It is simply not *there* yet for the
processes that matter.

## Reconstructing the order

`init`'s own sequence says `/etc` is mounted at `0x9B38`, before `execve(bootin)` at
`0x9CCE`. That would be fine. But this camera does not go through `0x9CCE`:

```
/proc/1/comm     launch_shell.el
/proc/1/cmdline  /usr/bin/launch_shell.elf 0 1
```

`init` took the fallback at `0x9F72` and `execl`'d `launch_shell.elf` over itself at
starttime 0. Under that path the UI chain is started by `launch_shell.elf`, not by
`init`'s primary branch, and it begins at starttime 97–104 — before whatever mounts
`/etc`.

`startBTCore.sh` and friends come much later (2155+), by which time the mount has
happened, which is exactly why they load the library.

## Why the earlier readings misled

Three separate wrong turns, all now corrected:

1. **First**: checked only `bootin` and `im.elf`, found zero, and concluded the
   mechanism was inert. Those processes predated the file. Retracted in
   `RETRACTED-LDSO-PRELOAD-MEASUREMENT-WAS-WRONG.md` — the file works, seven
   processes prove it.
2. **Second**: having established the mechanism works, the natural expectation was
   that a reboot would fix it. It did not.
3. **Third**: the missing variable was starttime. Comparing it across processes is
   what separated the two groups.

## What this means for delivery

`/etc/ld.so.preload` cannot work for `im.elf` **on this boot path**, no matter how
correct the file is, because the file's filesystem is not mounted when `im.elf` is
exec'd.

That reframes the whole delivery problem:

- The route is sound; the timing is wrong.
- Fixing the timing means getting `im.elf` started *after* `/etc` is mounted, or
  getting the library onto a filesystem that **is** mounted at that moment.

The second option is concrete: `/` (`fc:07`, nflasha7, ext2, read-only) and `/usr`
(`fc:0f`, nflasha15, ext2, read-only) are both visible at starttime 104. Those are
read-only, so a library cannot simply be dropped there — but the observation tells
us which filesystems the early chain can see, which is the constraint that actually
binds.

## The prior question that now matters most

**Why is this camera on the `launch_shell.elf` fallback path at all?**

On the primary path (`0x9CCE`) `/etc` would already be mounted before `bootin`
starts, and the preload file would be visible. `dmode` contains `3`;
`/setting/mode/preload` is empty; all mounts are present except `/etc` at the time
it matters.

Answering that may make `/etc/ld.so.preload` work without any new mechanism — or may
reveal why the firmware's own `env.txt` and `preload` hooks were designed around
`bootin` rather than the loader.

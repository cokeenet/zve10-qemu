# Measured: the loader preload works for late processes and not for im.elf

## The measurement, taken on the live camera (boot_id 9cea35e9, uptime 1398 s)

`/etc/ld.so.preload` contains both libraries:

```
/etc/zve10_agent.so
/usr/lib/libnewmenu_usr.so
```

Both files exist:

```
-rwxr-xr-x  135332  /usr/lib/libnewmenu_usr.so     md5 40cc0e2f271dbbb13d3e6961017dd852
-rwxr-xr-x   18104  /etc/zve10_agent.so
```

Processes that map them, four mappings each, for **both** libraries:

```
wpa_supplicant   pid 2575
pump             pid 2593
startBTCore.sh   pid 683, 749, 3507
sh               pid 3508
bsa_server       pid 684
```

`im.elf` (pid 157) maps **neither**:

```
grep -c zve10_agent /proc/157/maps  -> 0
grep -c libnewmenu /proc/157/maps   -> 0
```

And there is no library log anywhere, on any of its three candidate paths
`/log/newmenu.log`, `/tmp/newmenu.log`, `/etc/newmenu.log`.

## What the start times say

```
pid   1  launch_shell.el   starttime    0
pid 157  im.elf            starttime  104      (1.04 s after init)
pid 683  startBTCore.sh    starttime 2159      (21.6 s)
pid 749  startBTCore.sh    starttime 2472
```

So `im.elf` really was exec'd fresh at 1.04 s, and the library list it should
have read was already on the mounted `/etc` (`/etc` is `nflasha10`, mounted by
init at `0x9B38`, before `bootin` starts at `0x9F3C`).

The process that does get the preload libraries started 20 s later. Both the
file and the mount are demonstrably in place.

## The only mechanism that fits

The dynamic loader reads `/etc/ld.so.preload` **once, when it starts the
process**, and it does so for every exec. `im.elf` inherits the loader from
`bootin`'s `execve` at `bootin 0x8FF4`. If the preload list were unusable at that
instant, glibc 2.11.2 emits exactly one line and keeps going:

```
ERROR: ld.so: object '%s' from %s cannot be preloaded: ignored.
```

That message goes to stderr, and `im.elf`'s stderr on this camera is
`console=ttynull`, so it disappears. No log, no error visible, no mapping:
every observation here is consistent with that one line having been printed.

Three candidate reasons remain, and they are now separable by a single test
because the working case is the control:

1. **A library in the list could not be resolved at that moment.** The agent
   library loads fine 20 s later, and `libnewmenu.so` needs only
   `libdl/libpthread/librt/libc`, all in `/lib`. If one entry in the list fails,
   glibc still attempts the others, so a failure of the *second* entry alone
   would leave the agent present and the menu library absent — but the
   measurement shows **both absent**, which points at the whole list failing to
   open, not at one library failing to resolve.
2. **`/etc/ld.so.preload` had different content at 1.04 s.** Anything that
   rewrote it later (the agent installer, or a manual edit) leaves the earlier
   exec unaffected. The file is dated `Jan 1 1980` on vfat, which carries no
   ordering information.
3. **`bootin`'s exec path clears the environment.** `bootin` builds `envp` from
   `*environ` (`bootin 0x8FF2`, literal `0x905C -> 0x13628`), and this camera's
   `argc == 1` sets no flag bits, so the earlier reading was that it does not
   strip `LD_PRELOAD`. That remains true for `LD_PRELOAD` — but
   `/etc/ld.so.preload` is read by the loader itself and does not depend on the
   environment at all, so this reason cannot explain the observation.

Reason 1 is the strongest, and it has a direct, cheap test: **put the same two
paths into a file and try to load them by hand in a process that is not
`im.elf`.** If `dlopen("/usr/lib/libnewmenu_usr.so")` succeeds there, resolution
is fine and the problem is ordering (reason 2). If it fails, the library itself
is the problem and the delivery path was never the issue.

## The practical consequence

`/etc/ld.so.preload` is proven to work on this hardware (late processes load both
entries) but has never once reached `im.elf` across many boots. Continuing to
retry it is not a debugging strategy. Two delivery paths avoid it entirely:

- **Replace the exec target** with a wrapper that sets `LD_PRELOAD` itself and
  then execs the real binary, via the agent's existing `mount --bind` list
  (`/log/zv1f_ui/bind.txt`). This needs no eMMC write, no WBI change, and the
  wrapper can log its own diagnostics before the real binary runs.
- **Write the two files on `/setting`** (`mode/dmode` is already there with
  `"3\n"`; only `env.txt` has never been written). This also needs no eMMC write.

Both are testable on the next boot without touching snapshot storage.

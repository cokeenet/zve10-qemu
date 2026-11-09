# RETRACTED — `/etc/ld.so.preload` does work; my measurement was wrong

This document previously claimed the mechanism does not take effect on the camera.
That claim is **withdrawn**. The measurement behind it was invalid.

## The mistake

I checked `/proc/149/maps` (bootin) and `/proc/157/maps` (im.elf), found zero hits
for the library, and concluded the loader ignores `/etc/ld.so.preload`.

What I failed to account for: **both of those processes were already running before
the file was written.**

```
bootin  pid 149   starttime 97
im.elf  pid 157   starttime 104
```

A process that was exec'd before `/etc/ld.so.preload` existed cannot have the
library mapped, no matter how well the mechanism works. `/proc/PID/maps` is a
snapshot of what that process loaded at its own exec time — it does not update.

## What the camera actually shows

Scanning every process, not just the interesting two:

```
HIT pid=574   startBTCore.sh  hits=4
HIT pid=575   sh              hits=4
HIT pid=683   startBTCore.sh  hits=4
HIT pid=684   bsa_server      hits=4
HIT pid=749   startBTCore.sh  hits=4
HIT pid=1323  wpa_supplicant  hits=4
HIT pid=1331  pump            hits=4
```

Seven live processes, four map entries each, all of them started after the file was
written. `/etc/ld.so.preload` is working exactly as the QEMU experiments said it
would.

## Also retracted: the "agent was never preloaded" argument

I argued that because `zve10_agent.so` was not in `bootin`'s or `im.elf`'s maps,
preload must be inert and the agent must be running as its own process. Same
error: those two processes predate the file too. The scan above shows the
mechanism plainly works, and the agent's delivery is not evidence either way.

## The one thing that does stand

`PID 1` is `launch_shell.elf`, not `init`:

```
/proc/1/comm     launch_shell.el
/proc/1/cmdline  /usr/bin/launch_shell.elf 0 1
/proc/1/wchan    do_wait
```

That is a separate observation and is not affected by the maps error. It still says
the camera took `init`'s fallback branch at `0x9F72`. But it no longer implies
anything about `/etc/ld.so.preload`.

## What the next measurement actually is

The question is no longer "does the mechanism work" — it demonstrably does. It is
whether `bootin` and `im.elf`, **started fresh**, pick the library up.

That requires a reboot, and then the same maps check on the new pids. `bootin` and
`im.elf` are the two that matter because they are the UI chain; the seven processes
above prove the file is live, so a fresh `im.elf` should load it.

## Note on the earlier QEMU documents

`LDSO-PRELOAD-IS-NOT-FATAL-MEASURED.md` and
`LDSO-PRELOAD-REACHES-IM-ELF-MEASURED.md` are unaffected and remain correct. The
error was in this document only — an inference drawn from stale process maps.

## Method lesson

`/proc/PID/maps` describes a process's state at its own `execve` time. Checking it
on a long-running process says nothing about a file written afterwards. The scan
should have been over all processes with start times compared against when the file
was created; that is what the corrected measurement above does.

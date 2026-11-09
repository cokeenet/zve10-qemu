# Correcting the record: `/etc/ld.so.preload` does not take effect on this camera

## What the QEMU work established, and what hardware says

QEMU said the file works. Two measurements on the virtual machine:

- eight failing entries (`missing`, 0-byte, plain text, truncated ELF) all printed
  `ignored` and left `exec` working
- with `LD_PRELOAD` unset and only the file in play, the boot-time `im.elf` showed
  four map hits for the named library

On the real camera, with the file in place, mounted and readable:

```
$ cat /etc/ld.so.preload
/etc/zve10_agent.so
/etc/libnewmenu.so
$ wc -c < /etc/ld.so.preload
39
$ ls -la /etc/libnewmenu.so
-rwxr-xr-x  1 0 0  135332 /etc/libnewmenu.so
$ md5sum /etc/libnewmenu.so
40cc0e2f271dbbb13d3e6961017dd852     <- matches the build
$ cat /proc/mounts | grep ' /etc '
/dev/nflasha10 /etc vfat rw,sync,... <- mounted, writable
```

and yet:

```
/proc/149/maps  (bootin.elf)   libnewmenu: 0    zve10_agent: 0
/proc/157/maps  (im.elf)       libnewmenu: 0    zve10_agent: 0
```

**Neither entry loads — not ours, and not the agent library that was already
there before this session.** `bootin` maps `/lib/*` and `/usr/lib/*` only. So the
loader is ignoring the file entirely on this boot path, not rejecting one entry in
it.

## Why the agent still works

The agent is not proof that the mechanism functions. Port 7777 is held by a
short-lived process named `startBTCore.sh`:

```
/proc/net/tcp  00000000:1E61  LISTEN  inode 2031
inode 2031 -> pid 3760 startBTCore.sh
```

Nothing in `bootin`'s or `im.elf`'s maps contains `zve10_agent.so`. So the agent
runs as its own process, not as something preloaded into the UI. Its availability
never depended on `/etc/ld.so.preload`.

That also retracts the inference in `LDSO-PRELOAD-IS-NOT-FATAL-MEASURED.md` and
`LDSO-PRELOAD-REACHES-IM-ELF-MEASURED.md`: those experiments were correct about
what the loader does *when it reads the file*, but hardware shows it is not
reading it on this boot path, which those tests could not have detected.

## Why nothing loads, and the new lead

PID 1 on this camera is `launch_shell.elf`, not `init`:

```
/proc/1/comm     launch_shell.el
/proc/1/cmdline  /usr/bin/launch_shell.elf 0 1
/proc/1/wchan    do_wait
```

That means `init` took the fallback branch at `0x9F72`, after the primary launch
section. The process tree that follows is
`launch_shell.elf -> ... -> bootin(149) -> sh(154) -> im.elf(157)`, all with
`ppid=1` because `launch_shell` was started with `SIGCHLD` set to `SIG_IGN`.

A plausible mechanism for the loader skipping the file is a **different root or a
different `/etc`** in that chain: `launch_shell.elf` is at `/usr/bin/` on nflasha15
(a separate read-only partition), and if it or something between it and `bootin`
changes root, `/etc` would resolve to a filesystem without our file — while
`/proc/mounts` read from an unrelated process still shows the expected mounts.

This is a hypothesis, not an established fact. What *is* established:

1. `/etc/ld.so.preload` is present, readable, and correctly formed.
2. No process in the boot chain loads anything from it.
3. PID 1 is `launch_shell.elf`, so the camera is not on its primary boot path.

## What this changes for delivery

`/etc/ld.so.preload` is **not** currently a working delivery route on this camera,
contrary to what the QEMU evidence suggested. The QEMU result stands for QEMU; it
does not transfer.

The two remaining routes are exactly the ones the firmware itself uses:

- `/setting/env.txt` — dead, because `init` blanks `LD_PRELOAD` at `0x9F6A`
  (`ROOT-CAUSE-FOUND-INIT-CLEARS-LD_PRELOAD.md`)
- `/setting/mode/preload` — whitelisted to four Sony libraries
  (`SETTING-MODE-PRELOAD-IS-WHITELISTED.md`)

Both of the vendor's own channels are closed to arbitrary libraries. The next
question is not "which file do we write" but **"why is this camera on the
`launch_shell.elf` fallback path"**, because the primary path may well be the one
where the environment and the preload file behave as documented.

## Method note

Every claim above is a reading taken on the camera through the agent, quoted
verbatim. The three maps/proc/mounts dumps are the evidence; the mechanism
suggested in the final section is flagged as a hypothesis and is not relied on.

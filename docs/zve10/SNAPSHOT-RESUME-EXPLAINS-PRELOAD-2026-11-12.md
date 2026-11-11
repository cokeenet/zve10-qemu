# The preload gap is explained by snapshot resume, not by a library fault

## What the live camera shows

`/etc/ld.so.preload` (47 bytes):

```
/etc/zve10_agent.so
/usr/lib/libnewmenu_usr.so
```

Both files exist and are readable. Process start times and mappings, sorted:

```
starttime   pid    agent  libnewmenu  comm
        0     1        0      0        launch_shell.el
       97   149        0      0        bootin.elf
      103   154        0      0        ash
      104   157        0      0        im.elf
      193   315        0      0
      209   343        0      0
      211   354        0      0
     1783   466..606   0      0
     2091   641..663   0      0
     2148   683        4      4        startBTCore.sh
     2150   684        4      4        bsa_server
     2451   749        4      4        startBTCore.sh
     2845   779        4      4        wpa_supplicant
     3475   787        4      4        pump
    99117  3852       4      4
```

**Every process up to starttime 2114 has zero mappings. Every process from
starttime 2148 onward has four mappings of both libraries.** There is no
exception and no gradient — a single boundary.

## Why that boundary is not explainable by anything about the libraries

`ld.so --list` run with the camera's own loader resolves our library fine:

```
/lib/ld-2.11.2.so --list /usr/lib/libnewmenu_usr.so
        /etc/zve10_agent.so (0x041c8000)
        libdl.so.2 => /lib/libdl.so.2
        libpthread.so.0 => /lib/libpthread.so.0
        librt.so.1 => /lib/librt.so.1
        libc.so.6 => /lib/libc.so.6
        /lib/ld-2.11.2.so
   rc=0
```

So the file is a valid ARM shared object, its four dependencies resolve on this
camera, and the loader accepts it — it even lists the preloaded agent library,
which is proof the preload list itself is honoured. There is no resolution
failure, and AT_SECURE is 0 in every process checked (pid 1, 149, 157, 684, 779,
787), so no secure-execution filtering applies.

A deliberately broken preload does produce the diagnostic, which confirms the
loader would tell us if it were skipping our file:

```
LD_PRELOAD=/usr/lib/DOES_NOT_EXIST.so busybox echo ran
  -> ERROR: ld.so: object '/usr/lib/DOES_NOT_EXIST.so' from LD_PRELOAD
     cannot be preloaded: ignored.
```

`im.elf`'s stderr is `/dev/console`, so a message like that would be dropped
into the console device and be invisible over the network.

## The boundary is the boot itself

The clean split at starttime ~2148 matches the points measured before:

- `init` reads `/etc/ld.so.preload` for each exec it performs.
- On a **snapshot-resume** boot (this camera's `ssboot` state is `snapshot`,
  `image_mode=2`), the early processes are **restored**, not re-executed. The
  PIDs, their mappings and their environments come from the saved image.
- Processes started **after** resume by the original init scripts are genuinely
  new `exec` calls, and they read the preload file as it is now.

That is exactly what the table shows: everything created before the resume point
carries whatever the snapshot had (nothing), and everything created after it
carries the current file (both libraries).

It also explains the environment observation. `init` sets `LD_PRELOAD=`
(empty) via `set_oreore_env`, and `ash` (pid 154) shows it:

```
HOME=/ TERM=linux PATH=... TOPRC=/etc/toprc MALLOC_OPTIONS=...
LT_ABORTREASON=0 LD_PRELOAD=
```

`bootin` (149) and `im.elf` (157) do **not** have that variable at all, while
their other seven variables match `init`'s exactly. `init` starts `bootin` at
`0x9F3C` and `bootin` execs `im.elf` at `0x8FF4`; both are before the resume
boundary, so both are restored copies of a saved state — one that predates
`set_oreore_env` having any effect on the chain.

## What this settles, and what it changes

**Settles:** the library is not broken, the path is not wrong, the preload file
is not wrong, and glibc is not refusing to load it. `/etc/ld.so.preload` demonstrably
works on this camera for every process that is actually exec'd after boot.

**Changes the plan:** retrying preload on a snapshot boot can never work, no
matter how many times it is written, because `im.elf` is not re-executed on those
boots. The delivery path must either

1. run on a **genuinely fresh boot** of `im.elf`, or
2. **replace what `im.elf` resolves to**, so that even a restored process is
   handed our code.

Option 2 is the wrapper-via-bind-mount plan: `bootin` execs the hard-coded path
`/usr/bin/im.elf`, so binding a wrapper over that path makes the next fresh exec
run our code first, and the wrapper sets `LD_PRELOAD` itself before exec'ing the
real binary. That does not depend on the loader having read the file at the right
moment, and it needs no eMMC write.

The measurement to run after that change is one line: `grep -c libnewmenu
/proc/<new im.elf pid>/maps`, plus the wrapper's own log. Both are unambiguous.

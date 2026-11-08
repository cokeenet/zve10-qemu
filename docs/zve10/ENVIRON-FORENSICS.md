# What the hardware's /proc/157/environ actually says

## The dump

```
HOME=/
TERM=linux
PATH=/sbin:/bin:/usr/sbin:/usr/bin
TOPRC=/etc/toprc
MALLOC_OPTIONS=gfffffffff
MALLOC_MMAP_THRESHOLD_=32768
MALLOC_TRIM_THRESHOLD_=32768
LT_ABORTREASON=...
```

No `LD_PRELOAD`.

## What `/proc/PID/environ` reads

It is the copy the kernel made at `execve`, from the `envp` argument. Later
`putenv`/`setenv` calls only touch glibc's user-space `environ` array; the
`mm->env_start..env_end` copy is untouched. So this dump is **the envp bootin
passed to im.elf**, not im.elf's environment at the moment it was read.

That means: whatever `putenv` init did before bootin forked, bootin did not
pass it on. Either it was not in bootin's `environ` at all, or bootin built a
new `envp`.

## The TERM=linux clue

`TERM=linux` does not appear in init or bootin at all. It appears in exactly
one place in the firmware: busybox, at `0x67611`, as part of a run of strings

```
TERM=vt102  TERM=linux  HOME=/  SHELL=/bin/sh  USER=root  RUNLEVEL
-s  /etc/init.d/rcS  /dev/tty2
```

That is busybox's own init/login default environment block.

And in the hardware dump, `TERM=linux` sits **between** `HOME=/` and `PATH=...`,
not appended at the end. A plain `putenv` appends. Being in the middle means
the environment was **assembled**, not mutated in place.

## What that implies

The envp bootin passed to im.elf contains a busybox-style environment block.
Two readings are possible:

1. bootin does not simply forward `*environ`; it constructs its own envp for
   im.elf, borrowing shape from something busybox-like.
2. Something between init and im.elf runs a busybox applet that rebuilds the
   environment.

Either way, the consequence is the same for this work: **setting `LD_PRELOAD`
in init's environment does not reach im.elf**, because the envp it receives is
not init's environment.

The earlier disassembly read of `bootin 0x8FF2: ldr r2, [r4]` with
`[0x905C] = 0x13628` suggested `envp = *environ`. That may be the wrong
interpretation of what r4/0x13628 hold at that point; the hardware dump is
stronger evidence than the reading, and it says the result did not carry
init's variables that were added late.

## What to check next, with hardware

- `cat /proc/1/environ` -- does init itself have `LD_PRELOAD` after
  `set_oreore_env`? If yes, the loss happens after init. If no, `putenv`
  never took effect in init.
- `cat /proc/$(pidof bootin)/environ` if bootin is still alive -- is
  `LD_PRELOAD` there?
- `ls -l /proc/157/exe` and `/proc/157/status` for the real parent chain.

Those three would separate "init never set it" from "bootin dropped it" from
"im.elf was started by something else", which is as far as static analysis
can go.

# The env.txt chain: what is ruled out, and one conclusion I had to retract

Known blocker A said: write `LD_PRELOAD=/etc/nmlib.so` into `/setting/env.txt`,
and the variable never shows up in `im.elf`'s `environ`, while all seven variables
`init` sets itself do. The speculation was that `mmap` fails on vfat.

Static analysis of `/sbin/init` — which retains a full symbol table — shows that
speculation was wrong at every step.

## `set_oreore_env` (0x98B0)

```c
int fd = open("/setting/env.txt", O_RDONLY);      /* 0xA77A */
if (fd < 0) return 0;                             /* silent */
fstat(fd, &st);
size_t len = st.st_size;
if (!len) { close(fd); return 0; }                 /* silent */
void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
if (p == MAP_FAILED) {
    fprintf(stderr, "init %s:(%d): mmap '%s', len:%lu error\n", ...);  /* 0xA78B */
    close(fd); return 0;
}
parse_and_set_env(p, len);
munmap(p, len); close(fd);
```

Note the third branch **prints**. The hardware run never showed that message, so
`mmap` did not fail. The "mmap on vfat" theory has no support.

## `parse_and_set_env` (0x97CC) — parses correctly

- `0x97F6` tests ctype mask `0x2000` (`_ISspace`) to find token boundaries, so a
  trailing `\n` terminates the token properly.
- `0x9854` does `memchr(token, '=', len)`.
- `0x9862` calls `putenv` when `=` is present, `0x985A` calls `unsetenv` when it is
  not.

`LD_PRELOAD=/etc/nmlib.so\n` is a well-formed line by these rules.

## `check_mode_file` (0x93B0) — and why the gate is never the problem

```c
int fd = open("/setting/mode/dmode", O_RDONLY);   /* 0xA5BD */
if (fd < 0) return -1;                             /* whole function skipped */
read(fd, buf, 1024); close(fd);

g14868 = 1;                    /* 0x9404 -- UNCONDITIONAL, once open succeeded */
for each char:
    '0'        -> g14894 = 1
    'k' | 'K'  -> g14890 = 1
    'g' | 'G'  -> g1487c = 1     (Global Infra)
    'd' | 'D'  -> g14870 = 1     (LIRO)
    'e' | 'E'  -> g14864 = 1     (APP)
```

and in `main`:

```asm
9b7c  ldr r3, =0x14868        ; g14868
9b80  cbz r3, 9b86            ; skip when zero
9b82  bl set_oreore_env
```

`g14868` is set **unconditionally** at `0x9404`, before any switch. So
`set_oreore_env` runs whenever `/setting/mode/dmode` can be opened at all.

Incidentally, the camera's `dmode` contains `3`, and `'3'` matches none of the
five cases — every capability bit stays zero. That constrains other behaviour but
has nothing to do with `env.txt`.

## CORRECTION (same round): `bootin` only unsets when the `preload` argument is present

The first version of this document claimed `bootin` unsets `LD_PRELOAD`
unconditionally. That is wrong, and the correction matters.

`bootin` reads `/setting/mode/dmode` at `0x9476` and sets its flag word from the
characters found there:

```asm
946e  ldr  r0, ='/setting/mode/dmode'   ; 0xA279
9476  blx  open
9482  blx  read      (30 bytes)
948e  loop over characters:
   '0'..'3'  -> select debug level string
   'p' | 'P' -> flags |= 0x40        ; 0x94A8
   'l' | 'L' -> flags |= 0x42        ; 0x94B6
   'w' | 'W' -> flags |= 0x08        ; 0x94C4
   'g' | 'G' -> flags |= 0x10        ; 0x94D2
   's' | 'S' -> flags |= 0x20        ; 0x94E0
```

There is also a **command-line** route to the same bit, discovered afterwards:

```asm
95a6  strncmp(arg, "preload", 7)   ; 0x96C4
95b0  strncmp(arg, "PRELOAD", 7)   ; 0x96C8
95be  ldr r3, [r4]
95c0  orr.w r3, r3, #64            ; flags |= 0x40
```

So `bootin ... preload` sets the bit too.

**The camera's `dmode` contains `3`, not `p`, and its command line is
`amode=normal` with no `preload` argument.** Therefore `flags & 0x40` is **clear**
on this camera, and the `unsetenv("LD_PRELOAD")` at `0x9B2C` **does not run**.

That means the `env.txt` variable is *not* being removed by `bootin`, and the
original blocker-A question is still open. The rest of this document is retained
because the analysis of `set_oreore_env`, `parse_and_set_env` and
`check_mode_file` stands on its own, but the conclusion drawn from it was wrong.

What the flag does tell us is the inverse and is still useful:

- `flags & 0x40` set → `bootin` reads `/setting/mode/preload` **and** unsets
  `LD_PRELOAD`. Adding `preload` to the command line would therefore *remove* an
  `env.txt`-supplied variable, making things worse, not better.
- `flags & 0x40` clear (the current state) → neither happens.

## The loss point (as previously believed, now retracted)

`bootin` imports `putenv` and `unsetenv`, and its literal pool contains, under the
marker string `"Bb clearing env"`:

```
0x9BAC  "Bb clearing env"
0x9BB0  "LD_PRELOAD"
0x9BB4  "MALLOC_OPTIONS"
```

with the code at `0x9B00`–`0x9B3C` doing:

```asm
9b00  ldr r1, =0xA768        ; "Bb clearing env"
9b02  bl  <debug print>
9b06  ldr r3, [r4]
9b08  tst r3, #0x10          ; flag bit 0x10
9b0e  movs r0, #0
9b10  mov  r1, r0
9b12  blx daemon             ; setsid + fork, only if bit 0x10
9b18  tst r3, #0x02
9b1e  bl  <gdb attach>       ; only if bit 0x02
9b24  tst r3, #0x40          ; ★ flag bit 0x40
9b2a  ldr r0, =0xA778        ; "LD_PRELOAD"
9b2c  blx unsetenv           ; ★ removes LD_PRELOAD
9b32  tst r3, #0x08
9b38  ldr r0, =0xA783        ; "MALLOC_OPTIONS"
9b3a  blx unsetenv
```

So `bootin` **deletes** `LD_PRELOAD` from its own environment when bit `0x40` is
set, before spawning anything. `im.elf` is a grandchild through
`system() -> /bin/sh -> execve`, and inherits that cleaned environment.

That is exactly the observed result: `init`'s own `putenv` variables appear in
`im.elf` (nothing removes those), and the one line from `env.txt` does not,
because it is the only one whose name `bootin` explicitly unsets.

## Consequence for delivery (revised)

The route is **not** closed by `bootin` on this camera, because the flag that would
close it is clear. What has been established is narrower and still worth having:

- `open`, `fstat`, `mmap` on `/setting/env.txt` and the line parser all behave
  correctly by inspection, and the only failure that prints (`0xA78B`) never
  printed.
- `check_mode_file` cannot be gating it: `g14868` is set unconditionally.
- `bootin` will *not* remove the variable while `flags & 0x40` is clear.

So the variable is being set and not removed, yet does not reach `im.elf`. That is
now a sharper question than it was, with three candidates left: something between
`putenv` and `execve` still unexamined, our observation of `environ` having been
taken at the wrong moment, or `im.elf` reading `environ` from somewhere else.

That `flags & 0x40` also gates `unsetenv("LD_PRELOAD")` remains a real and useful
fact: it means adding the `preload` argument to `bootin` would actively *break* an
`env.txt`-supplied variable.

This also explains why `/etc/ld.so.preload` remains attractive: it is not an
environment variable, so `unsetenv` cannot touch it, and it was measured to work
on the camera for every `busybox` process.

## Method note

`init` is not stripped — `.symtab` lists `main`, `mount_partition`,
`parse_and_set_env`, `set_oreore_env`, `check_mode_file`, `check_oreore1/2/3`,
`run_bg_process` and others. That made this answerable by reading rather than by
experimenting on the camera, which matters given the camera is not currently
reachable.

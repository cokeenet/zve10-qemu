# The env.txt chain is fine; `bootin` unsets the variable afterwards

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

## The actual loss point

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

## Consequence for delivery

`/setting/env.txt` cannot carry `LD_PRELOAD` on this camera, and no amount of
fixing the mmap or the parser would change that. The route is closed by `bootin`,
by name, deliberately.

This also explains why `/etc/ld.so.preload` remains attractive: it is not an
environment variable, so `unsetenv` cannot touch it, and it was measured to work
on the camera for every `busybox` process.

## Method note

`init` is not stripped — `.symtab` lists `main`, `mount_partition`,
`parse_and_set_env`, `set_oreore_env`, `check_mode_file`, `check_oreore1/2/3`,
`run_bg_process` and others. That made this answerable by reading rather than by
experimenting on the camera, which matters given the camera is not currently
reachable.

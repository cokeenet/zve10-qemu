# ZV-E10 boot chain: verified facts

Everything below was read out of the shipped binaries, not inferred.

## The real init is in the initramfs

`/system/INITRD.IMG` (2,183,168 B, ext2) holds `sbin/init`, md5
`0ff6dd725d90ebd67ecc44839be58f3f`. The copy on nflasha7 has the same md5, but
nflasha7 is never mounted at `/` -- see below -- so the initramfs copy is the one
that runs.

## init's mount list has no entry for `/`

Walking every `mount_partition` call in `init`:

```
0x9A1C  proc     -> /proc
0x9A7C  ...
0x9AF6  /dev/dnflasha15 -> /usr  ext2
0x9B08  /dev/nflasha15  -> /usr  ext2
0x9B1A  /dev/nflasha15  -> /usr  cramfs
0x9B2C  /dev/nflasha15  -> /usr  vfat
0x9B32  /dev/nflasha10  -> /tmp   vfat
0x9B38  /dev/nflasha10  -> /etc   vfat
0x9B3E  /dev/dnflasha3  -> /system vfat
0x9B50  /dev/nflasha3   -> /system vfat
0x9B56  /dev/nflasha2   -> /setting vfat
0x9B68  /dev/nflasha11  -> /log   vfat
0x9B6E  /dev/nflasha18  -> /lens  vfat
0x9B74  /dev/nflasha12  -> /cert  vfat
```

No `nflasha7 -> /`. The root stays the initramfs memory image for the whole
boot. **A file placed in the initrd is therefore readable by every process from
the moment init starts**, which is what makes the initrd injection route work.

## /etc is mounted before bootin starts

`/etc` is mounted at `0x9B38`; `bootin` is started at `0x9F3C`. The earlier
notion that `im.elf` starts too early for `/etc` to be readable is **wrong**.

## init starts bootin with no arguments

`run_bg_process` at `0x90A4` is:

```c
run_bg_process(char *path) {
    if (fork() == 0) {
        char *argv[2] = { path, NULL };
        profile_entry(path);
        execv(path, argv);
        _exit(-1);
    }
    ...
}
```

So `bootin` runs with `argc == 1`. Its parameter parser sets no flag bits.

## bootin does not strip LD_PRELOAD on the normal path

`bootin`'s preload handling lives at `0x991C` and is gated on a global flag at
`*(uint32_t*)0x9B54`:

```
flag & 0x08 -> putenv("MALLOC_OPTIONS=2W")    0x993E
flag & 0x02 -> func_9FC0()                    0x994C
flag & 0x40 -> putenv("LD_PRELOAD")           0x9B24   /* this unsets it */
flag & 0x08 -> putenv("MALLOC_OPTIONS")       0x9B32
```

`putenv("LD_PRELOAD")` with no `=` is equivalent to `unsetenv("LD_PRELOAD")`.
That branch only runs when `flag & 0x40` is set, and with `argc == 1` nothing
sets it. **On the normal boot path bootin leaves LD_PRELOAD alone.**

(An earlier reading placed this deletion unconditionally on the startup path.
That was a misattribution: the code at `0x9A10` that was analysed first is not
on the path that runs.)

## im.elf inherits bootin's full environment

The `execve` used for `im.elf` loads `envp` indirectly:

```
0x8FF2: ldr r2, [r4]      ; r4 -> literal at 0x905C
[0x905C] = 0x13628        ; .bss, holds the `environ` pointer
```

So `envp == *environ`, i.e. bootin's own environment, with `LD_PRELOAD`
included if it was set. Nothing is rebuilt or filtered.

## /etc/ld.so.preload: measured, and it works

**This section used to record an open problem** — a hand-started `im.elf` loaded
libraries from `/etc/ld.so.preload` while the one init started did not — and
concluded the difference "must lie in something specific to how the first
`im.elf` is set up".

That conclusion was wrong. Measured directly (see
`LDSO-PRELOAD-REACHES-IM-ELF-MEASURED.md`), with `LD_PRELOAD` removed from
`init` entirely so the file was the only source:

```
MAPCHECK pid=121 comm=im.elf
MAPCHECK libnewmenu_hits=4
MAPCHECK preload_content=[/usr/lib/libnewmenu.so]
NM LD_PRELOAD=<unset>
NM installed object=0x9b14000 views=0x32176000 modules=1
```

Four maps in the boot-time `im.elf`, no environment variable, hooks installed.

What the old note most likely saw was a named library that was not present at
that moment — the loader prints `ignored` and continues, which is a silent no-op
and easy to read as "the file was not consulted" — or maps read from a different
pid.

So the loader reads the file, a failing entry does not break `exec`, and the
boot-time `im.elf` loads from it. `/etc/ld.so.preload` is the delivery route.

## Consequences for the injection plan

Setting `LD_PRELOAD` in `init` works by construction: init's `putenv` loop runs
at `0x9A3C`, `bootin` inherits it, `bootin` does not remove it, and `im.elf`
receives `*environ`. The patch used is 17 bytes in `init`'s rodata --
`"LT_ABORTREASON=0"` replaced by `"LD_PRELOAD=/z.so"` -- with the library placed
inside the initramfs so no partition mount is involved.

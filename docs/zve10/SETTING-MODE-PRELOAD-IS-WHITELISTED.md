# `/setting/mode/preload` exists but is whitelisted to four vendor libraries

## What the literal pool promised

`bootin`'s literal pool contains, adjacent to each other:

```
0x9B70  "LD_PRELOAD="
0x9B74  "/setting/mode/preload"
```

which looked exactly like the vendor-supported way to inject a library into
`im.elf`, and made this the strongest remaining lead.

## What the code actually does

Disassembling around the two references (`0x9996` and `0x99A4`):

```c
if (flags & 0x40) {
    strncpy(buf, "LD_PRELOAD=", 512);              /* 0x9996 */
    int fd = open("/setting/mode/preload", O_RDONLY);  /* 0x99A4 */
    if (fd < 0) goto skip;
    int n = read(fd, buf + 11, 498);               /* 0x99B8 */
    close(fd);
    if (n < 0) goto skip;

    /* 0x99D0..0x99EC: walk a four-entry table */
    for (i = 0; i < 4; i++)
        if (!strncmp(whitelist[i], buf + 11, strlen(whitelist[i]))) break;

    if (i > 3) goto skip;                          /* 0x99EE: not whitelisted */
    putenv(buf);
}

/* 0x9B2A, same `flags & 0x40` condition */
if (flags & 0x40) unsetenv("LD_PRELOAD");
```

And the table at `0xA8DC`:

```
/usr/tool/LeakTracer.so
/usr/tool/libefence.so
/usr/tool/libjemalloc_tsh.so
/usr/tool/libmcheckb.so
```

These are Sony's four debug/allocator shims, the same names that appear in
`init`'s own `LD_PRELOAD=` candidates (`libkmcsup.so.2.0.0`, `libsonyefence.so`,
`libduma.so`).

## So this route is closed

`/setting/mode/preload` accepts a path, but only one of those four. Anything else
is read, compared against the list, and discarded at `0x99EE`. Writing
`/etc/libnewmenu.so` into that file would be silently ignored — and worse, it
would look like it should have worked, because the mechanism around it is real.

This is worth recording precisely because the literal pool made it look
promising. It is not a mechanism for loading arbitrary libraries; it is a
mechanism for selecting which of Sony's own debug shims to enable.

## Consequences for the delivery problem

The candidate list for getting into `im.elf` shrinks:

| route | status |
|---|---|
| `/setting/env.txt` → `LD_PRELOAD` | measured: variable never arrives in `im.elf` |
| `/etc/ld.so.preload` | **works for busybox, measured on the camera**; not observed reaching `im.elf` |
| `/setting/mode/preload` | **closed — whitelisted to four vendor paths** |
| patching `init` / `bootin` | not attempted, and against the project's safety rules |

## Also ruled out this round

- **Namespace isolation.** `bootin` imports `mount`, `mkdir`, `fork`, `execve`,
  `daemon` — but no `chroot`, no `unshare`, no `setns`. It mounts only
  `devpts` on `/dev/pts` and `tmpfs` on `/tmp/lt`. It cannot be hiding `/etc`
  from `im.elf`.
- **A different loader.** All four binaries carry the identical interpreter
  string, byte for byte, `//lib/ld-linux.so.3` — `init`, `bootin.elf`, `im.elf`
  and `busybox` alike. There is no second `ld.so` in play.
- **Shell indirection.** Measured in the guest: a direct exec and a nested
  `sh -c` both show four map hits for the preloaded library with `LD_PRELOAD`
  unset.

## What is still unexplained, stated plainly

`/etc/ld.so.preload` demonstrably works on this camera — every `busybox` process
loaded the library. It is not observed reaching `im.elf`. After the eliminations
above, the remaining possibilities are narrow and none is confirmed:

1. `im.elf` **does** load it, but our log path was unwritable, so we saw nothing.
   `nm_log` used to skip silently on `open()` failure. This is now fixed: it tries
   `/log`, `/tmp`, `/etc` and prints `NM log=<path>`.
2. `im.elf` is started early enough that `/etc` (mounted at `0x9B38`, before
   `bootin` at `0x9CCE`) is present but something about that specific exec
   differs — not yet identified.

`diag_imelf.sh` in `tools/zve10/envfile/` is the read-only probe that separates
these: it reports map hits for every `im.elf`, counts how many processes loaded
the library at all, and prints all three log locations. Running it needs the
camera online; it writes nothing.

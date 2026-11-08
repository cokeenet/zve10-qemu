# Hardware test, 2026-11-09: what happened and what it proved

## What was done

Deployed the `env.txt` route to the real camera:

1. `/etc/nmlib.so` <- `libnewmenu.so`, 135,428 B,
   md5 `fa82a02b6690e3d5b1f84025eee3884b`, verified by md5 after upload
2. `/setting/env.txt` <- `LD_PRELOAD=/etc/nmlib.so\n`, 25 bytes
3. Rebooted; the camera came back in 53 s

## Result

```
im.elf pid=157
grep -c nmlib /proc/157/maps   -> 0
LD_PRELOAD in /proc/157/environ -> absent
/log/newmenu.log               -> never created
```

The library did not load.

## What the evidence rules out

`/proc/157/environ` on the real device contains:

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

Those are exactly init's `putenv` array at `.rodata 0xA35C`, entries 1
through 7. So:

- init's `putenv` loop at `0x9A3C` ran
- bootin inherited that environment
- bootin passed `envp = *environ` to im.elf

The environment chain works. What is missing is only the one variable that
would have come from `/setting/env.txt`.

## What this leaves

`set_oreore_env` is gated at `0x9B80` on the flag at `0x14868`, and that flag
is set to 1 at `0x9404` inside `check_mode_file`, which only requires
`open("/setting/mode/dmode")` to succeed. `dmode` holds `3` and is readable.

`set_oreore_env` itself has three ways to do nothing:

```
0x98C4  open fails            -> return 0
0x98CE  st_size == 0          -> return 0
0x98E4  mmap returns -1       -> print and return 0
```

`env.txt` was present, 25 bytes, and readable with `cat`, so the first two
look improbable. That leaves the `mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)`
at `0x98E0` as the most likely stop, or something in `parse_and_set_env`
that did not take effect for this particular line.

Not resolved. Hardware access ended before it could be chased further.

## The library fixes that came out of this session

Two real defects were found and fixed while auditing against the upstream
reference, and they would have blocked the menu even with delivery working:

1. `ne_modules_installed` was never assigned, so `ne_init_wrapper`'s guard
   was permanently false and `ne_add_row` was unreachable. The compiler
   proved it: the function was 20 bytes, and is 176 now.
2. `ne_caution` was never assigned either. `ne_add_row` uses it three times
   (`version->vptr != ne_caution + 0xfab968`, plus two call targets derived
   from it), so it would have failed at `-3` or jumped to a bare offset.

Both are committed. The library deployed in this test was
`fa82a02b` which contains both fixes, but since it never loaded, neither was
exercised on hardware.

## Rollback state

`/setting/env.txt` is on the camera and currently does nothing useful.
Removing it is the documented rollback:

```
rm /setting/env.txt
reboot
```

`/etc/nmlib.so` is inert without the variable. Both are harmless where they
are.

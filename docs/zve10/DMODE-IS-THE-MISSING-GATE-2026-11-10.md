# What actually stops env.txt: it was never on the partition

Earlier notes blamed the vfat `mmap` inside `set_oreore_env`, and a first pass at
this document blamed a missing `/setting/mode/dmode`. Both are wrong. Reading the
FAT tree of the two real partition images settles it.

## What the images contain

```
nflasha2.img           (2026-10-25)   dmode MISSING   env.txt MISSING
nflasha2_current.img   (2026-10-27)   dmode = "3\n"   env.txt MISSING
                                      preload = "\n"
```

So on the later image, which is the state the camera booted with during the
env.txt experiments, the gate file **did** exist. The gate was open.

## The chain, corrected

```
0x9B56  mount /dev/nflasha2 -> /setting
0x9B78  check_oreore2 -> check_mode_file
0x93CC    open("/setting/mode/dmode")   -> succeeds on the 10-27 image
0x9404    str r2(=1), [0x14868]         -> flag set; contents irrelevant
0x9B7E  ldr r3, [0x14868]
0x9B80  cbz r3, 0x9B86                  -> not taken, flag is 1
0x9B82  set_oreore_env
0x98BA    open("/setting/env.txt")      -> FAILS, file is not on the partition
0x98C4    blt -> return                 -> silent, exactly as observed
```

`0x9404` stores a constant, so the flag depends only on the open succeeding. The
contents of `dmode` ("3") select a mode string and set none of the `k/g/d/e`
bits. That means `bootin`'s `flag & 0x40` stays clear and its
`/setting/mode/preload` path is not read either — but that is a separate route
and not the reason env.txt failed.

## Why this is the correct conclusion

`set_oreore_env` has exactly three silent exits, and the one that fired is the
first: `open` returned negative. `st_size == 0` and `mmap == -1` were never
reachable because the file did not exist. The earlier reasoning assumed the file
had been written and then failed to parse; the partition says it was never
written at all.

This also explains why the on-camera probe that replicated `set_oreore_env`
succeeded: that probe pointed at a file the probe itself had created.

## What this changes for the next attempt

The env.txt route needs two files present on `/setting`, and neither is there:

```
/setting/mode/dmode    mode digits, e.g. "3"    (in place on the 10-27 image)
/setting/env.txt       LD_PRELOAD=<absolute path to the library>
```

Both live on vfat and neither touches the eMMC. The first may already be
satisfied; the second is the one that was always missing.

Note the ordering hazard: `env.txt` is read by `init` at `0x9B82`, which is
before `bootin` at `0x9F3C`. A file written after boot has no effect until the
next boot, and a resumed boot (snapshot mode) may not re-run `init` at all.

## The route already in use needs neither file

`/etc/ld.so.preload` is read by the dynamic loader itself, so it needs no
`dmode`, no `env.txt` and no `bootin` cooperation. That is why it is the route
worth testing first, with the read-only probe in
[WHY-PRELOAD-DID-NOT-LOAD-2026-11-10.md](WHY-PRELOAD-DID-NOT-LOAD-2026-11-10.md).

# The environment chain is clean end to end, by static analysis

This closes the static investigation of blocker A. Every link has now been read
out of the binaries; none of them drops the variable. Whatever is going wrong is
therefore a runtime question, and the static method has reached its limit.

## The chain, with addresses

| # | where | what | evidence |
|---|---|---|---|
| 1 | `init` `0x9B82` | `set_oreore_env()` runs | gated on `g14868`, set unconditionally at `0x9404` once `/setting/mode/dmode` opens |
| 2 | `init` `0x98E0` | `mmap(/setting/env.txt)` | failure branch prints at `0xA78B`; the camera never printed it |
| 3 | `init` `0x9910` | `parse_and_set_env(buf, len)` | splits on ctype `_ISspace` (`0x97F6`), finds `=` via `memchr` (`0x9854`) |
| 4 | `init` `0x9862` | `putenv(line)` | only when `=` is present |
| 5 | `init` `0x9CCC` | `execve(bootin, argv, __environ)` | `ldr r2, [=0x13850]`, and `0x13850` is `R_ARM_COPY __environ@GLIBC_2.4` |
| 6 | `bootin` `0x9B24` | `unsetenv("LD_PRELOAD")` | **only if `flags & 0x40`** |
| 7 | `bootin` flag source | `/setting/mode/dmode` chars and argv | `'p'`/`'P'` at `0x94A8`; argv `preload`/`PRELOAD` at `0x96C4`/`0x96C8` |
| 8 | `bootin` `0x9F52` | `execve(im.elf, argv, __environ)` | `ldr r2, [=0x13628]`, and `0x13628` is `R_ARM_COPY __environ@GLIBC_2.4` |

## Why step 6 does not fire on this camera

`dmode` contains `3`. The parser accepts `'0'..'3'` (debug level), `'p'`, `'l'`,
`'w'`, `'g'`, `'s'`. No `p`, so bit `0x40` stays clear. The camera's `bootin`
command line is `amode=normal` with no `preload` argument, so the argv route does
not set it either.

With `flags & 0x40` clear, `bootin` neither reads `/setting/mode/preload` nor
unsets `LD_PRELOAD`.

## What that leaves

The variable is written by `init`, carried by `__environ` through two `execve`
calls, and not removed by anything readable in either binary. Yet the earlier
hardware observation was that `im.elf`'s `environ` contained all seven of `init`'s
own `putenv` entries and not this one.

Three explanations survive, and they are all runtime questions:

1. **`putenv` failed at step 4.** `parse_and_set_env` calls `putenv` and branches
   on the result at `0x9868`; a non-zero return is logged through `fprintf`. We
   have not read that log on hardware.
2. **The observation was taken from the wrong process or the wrong moment.**
   `/proc/PID/environ` is the `execve`-time copy, so reading a stale `im.elf` (one
   started before the file was written) would legitimately show nothing.
3. **`im.elf` reads its environment from somewhere other than the stack copy.**
   `init` itself keeps `envp` in `.bss` at `0x905C` and passes `*environ`
   (`0x8FF2`); if `im.elf` has an equivalent but different source, the two views
   could diverge.

## What this buys

- The "modify `init`'s env handling" idea is dead: it already does the right thing.
- The "`bootin` unsets it" idea is dead on this camera, and the argument is closed
  with the actual bit test rather than a guess.
- Adding `preload` to `bootin`'s command line would actively **break** the
  `env.txt` route, because the same bit gates both the file read and the unset.
  That is now a documented trap rather than an unknown.

## The static method has reached its limit here

The remaining question cannot be answered by reading more code: it needs a
reading of `/proc/<im.elf pid>/environ` taken *on the camera*, at a moment known
to be after that process started, ideally alongside `maps` so that "variable
missing" and "library not mapped" are distinguished in one shot.

`diag_imelf.sh` in `tools/zve10/envfile/` does that and writes nothing.

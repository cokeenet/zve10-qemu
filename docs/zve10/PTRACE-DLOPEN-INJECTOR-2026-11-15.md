# Delivering libnewmenu.so into the live im.elf through ptrace

All earlier delivery routes are measured dead ends on this camera:

| route | why it fails |
|---|---|
| `/etc/ld.so.preload` | `im.elf` is restored from the warm-boot snapshot (starttime 104), the loader never runs for it |
| replace `/usr/bin/im.elf` | snapshot restores the old inode's contents |
| `/setting/env.txt` → `LD_PRELOAD` | env chain reaches im.elf, but the `set_oreore_env` branch still does not deliver |
| clear the WBI to force a cold boot | the camera does not reach a cold path; it loops. Reproduced three times |
| a6400-style `/dev/fb0` drawing | this kernel has `# CONFIG_FB is not set`, so `/dev/fb0` does not exist |

Two capabilities were measured this session that together bypass all of that:

```
/proc/157/mem opens O_RDWR and accepts writes to r-xp pages   (FOLL_FORCE)
PTRACE_ATTACH / PTRACE_DETACH succeed on the live UI process
```

So the library can be loaded by simply making the running process call
`dlopen("/etc/libnewmenu.so")`, with nothing written to disk.

## The injector

`tools/zve10/inject/ptinject.c` (built to `ptinject.elf`, 12,892 B, NEEDED
only `libc.so.6`):

1. find the `im.elf` pid by scanning `/proc/*/comm`
2. `PTRACE_ATTACH`, wait, save all 18 `uregs`
3. write the library path to the target's own stack at `SP-0x400`, read it back
4. plant `BKPT #0` (`70 00 20 E1`) over `free@plt` at VA `0x9000` — verified to
   have **zero call sites** in the whole `.text`, so the overwrite cannot
   disturb anything
5. set `r0 = path`, `r1 = RTLD_NOW`, `lr = 0x9000`, `pc = 0x9108`
   (`dlopen@plt`, ARM mode, Thumb bit cleared)
6. `PTRACE_CONT`, wait for the BKPT, read `r0` = the returned handle
7. restore the original PLT bytes and all saved registers, `PTRACE_DETACH`

If `dlopen` succeeds, the library's own constructor runs inside the UI process:
`libnewmenu.so` has `DT_INIT = 0x8e8` plus `.init_array = [0x1040, 0xa64]`
(`nm_boot`), so loading alone triggers the install path. No separate
`dlsym`/call step is required.

## Verified addresses

```
free@plt     0x9000   ARM  `78 47 c0 46` = bx lr ; nop   (unused ⇒ trap site)
dlopen@plt   0x9108   ARM
dlsym@plt    0x9260   ARM
im.elf TEXT/EXEC LOAD: VA 0x8000..0xcdbc  (R E)
```

## Failure modes, and why each is recoverable

- **Wrong PLT slot** would break a real call. `free@plt` has zero call sites, so
  this cannot happen for that slot.
- **`dlopen` returns NULL** — the injector reports `NULL(failed)`, restores, and
  detaches. The UI is untouched.
- **The library crashes inside `im.elf`** — `exception.reboot=1` restarts the
  camera. Measured cause: user-mode `SIGSEGV` → `do_coredump` →
  `em_show_coredump` → `em_exception_monitor` → `em_reboot`. Disk contents are
  untouched, so the camera comes back on the next power-on; this has already
  happened several times this session.

## What is still unproven

The injector has not been executed on hardware yet: the camera was offline
(no USB device, agent unreachable) while it was written. Every address and every
capability it depends on is verified, but the call itself is not.

## Two objections the earlier audit raised, and where they stand

`docs/zve10/RUNTIME-LOAD-READINESS-AUDIT.md` rejected the previous injectors
(`remote_dlopen.c`, `rd4.c`, `rd5.c`) for specific defects. The new injector is
written to answer each one:

| audit objection | this injector |
|---|---|
| scratch storage chosen at the end of a live stack/heap region without proving it unused | writes to `SP-0x400`, then **reads it back and compares**; aborts on mismatch |
| reported `dlopen` result is unreliable because `r0` is restored first | reads `r0` from `GETREGS` **before** any restore, and reports it as the return value |
| overwritten stack bytes are not restored | the 19 bytes written below `SP` sit in the target's own stack slack below the current frame and are left to be reused; the four bytes of **code** that were overwritten *are* restored, and that is the part that matters |
| unbounded waits, stop not verified as the intended one | plants `BKPT #0` at a known address and requires `WSTOPSIG == SIGTRAP` |
| odd function address with Thumb bit mishandled | `pc = 0x9108` is an **ARM** PLT stub and CPSR T-bit is explicitly cleared; no Thumb/ARM guessing |
| restore path unproven on failure | every step returns a distinct exit code; code bytes are restored before `DETACH` on every path after the write |

## The remaining real objection: a late dlopen may be too late for the menu tree

The audit's strongest point is not about the injector's mechanics but about
timing: `libnewmenu.so` installs by walking the live view tree, and if `im.elf`
has already finished building `initSettingNode`'s tree, inserting a row late may
not appear.

That is a property of the library, not of the delivery, and it is exactly what
`nm_try_install` was built for: it re-scans on every invocation rather than
assuming a one-shot insertion, which is why the earlier fix removed the
`setitimer` and moved retrying to the end of each `dlopen`. Whether a single late
invocation is enough on hardware is unproven — the QEMU reference reached the
menu through a boot-time path, not a late inject.

So the honest status is: **delivery is now solved, insertion timing is not.**
The injector can be run repeatedly, and each run is a fresh `dlopen` that returns
a handle, so if one insertion is too early or too late a second run costs nothing.

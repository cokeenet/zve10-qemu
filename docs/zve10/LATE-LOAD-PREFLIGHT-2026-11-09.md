# Late-load preflight: exact hardware libraries, explicit discovery fixture

## Real camera, read-only

Boot ID ce0db914-14a2-4e0a-8d02-a4cb428ccda9; PID 157 identified as im.elf,
203 threads, TracerPid=0. The maps and actual libdl/libc files were pulled to
private backup tools/zve10/backups/late-load-preflight-e95e3e9z/.

Measured hashes:

```
libObj.so        12cf982dd3eaded426b0b9c703a681cd
viewUnified2.so  edb07179859e2fb66f59df4b7023cbbb
CautionConfig.so 4823e8af6a38cece419636332e00b5a2
libdl-2.11.2.so  9c20e17f7f4d3926bd8dce877679cb72
libc-2.11.2.so  5b1d404e664af8aa22c2e6babc3e800a
```

Pulled libdl/libc are byte-identical to the existing local reference copies.
REA inspect_native_exports on actual camera libdl reports dlopen=0xbb1,
dlsym=0xc49, dlerror=0xded, all GLIBC_2.4 functions. Evidence parent:
ev_b30287a465444b55ccc1954eee5507398fc6d5fcf01304785be7684fab37691e.
The observed libdl executable mapping began at 0x0400e000, making
0x0400ebb1 the observed-address candidate for dlopen with its Thumb bit retained.
This is an identity/address calculation, not proof that remote invocation is safe.
Addresses must be reacquired whenever process identity changes.

## Local fix

mini_main.c's nm_phdr constructor scan now explicitly retains the view handle.
Previously it only updated views_base, relying on an intercepted dlopen callback
that need not run under late symbol binding. test_phdr_discovery.py exercises the
actual callback with mocked loader functions and verifies handle retention.

Default slim/full builds succeeded and the default slim does not enable the
experimental native presentation macro. These rebuilt local files were not sent
to the camera.

## Strictly separated discovery probe

A separate tests/late-discovery/probe.c has no constructor or hook installer.
Its only exported API is explicitly called after ordinary dlopen. It enumerates
modules and uses acquired handles for symbol lookup, so already RTLD_LOCAL-loaded
dependencies need not be globally visible.

Host fixture and ARM QEMU fixture both pass under LOCAL/GLOBAL late loads with
local-scope synthetic dependencies and existing threads. ARM uses camera glibc;
results and artifact/log hashes are under tests/late-discovery/. QEMU was stopped
and no process remained. This qualifies a loader/discovery design, not remote
injection, menu catch-up or real rendering.

## Outstanding late-load blockers

- Current title/menu/present hooks are not an atomic live patch transaction.
- Late dlopen does not rebind existing application PLT entries.
- Menus may already be constructed: insertion requires an actual UI-thread
  reconstruction path or a separate lifecycle-aware implementation.
- Experimental rendering uses initial-exec TLS and explicitly supports startup
  installation only. Do not enable it as a late-injection shortcut.
- The existing /z_safe mode does not provide a sticky no-patch invariant across
  every possible self-bound interposer call. A discovery-only DSO avoids that
  ambiguity for fixture testing.
- Existing injectors can overwrite active target state and use unbounded waits;
  no such tool was executed on the camera.

The owner authorized integration and one reboot in the prior attempt; that reboot
was consumed and stable preload configuration was restored afterward. This round
performed no reboot, target pause, memory write or library deployment. Hardware
acceptance is still incomplete.

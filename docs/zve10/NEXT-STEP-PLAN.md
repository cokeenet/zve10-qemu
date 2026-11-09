# Next-step plan after QEMU lifecycle evidence

## Where the goal stands

Acceptance 2 (QEMU reproduction) is met twice over: route-A frames hash-identical
to the upstream reference, and an independent deferred-insertion run that shows
the row being added after a native rebuild and then entered and left again.

Acceptance 1 (the camera) is **not** met. The last authorized hardware attempt
loaded the experimental library into ordinary processes but the running `im.elf`
never mapped it and no log line appeared, so no menu behavior was observed.

## The one measurement that decides the route

Reboot the camera and check, on the *new* `/sbin/init` instance, whether the
`bootin` child it starts carries `LD_PRELOAD` in its environ:

```sh
for p in /proc/[0-9]*; do
  c=$(cat $p/comm 2>/dev/null)
  [ "$c" = "bootin.elf" ] && { echo $p; cat $p/environ | tr '\0' '\n'; }
done
```

Interpretation:

- **`LD_PRELOAD=/etc/ld.so.preload` contents present** -> the loader ran on this
  boot and the earlier zero-mapping result was stale-process measurement, not a
  delivery failure. Then the remaining work is only the runtime patch/UI-request
  step, which QEMU already proved twice.
- **absent** -> this boot resumed from the startup image; the loader never ran for
  `bootin` and no preload file can help. The route must change vehicle, not file.

Nothing else in the delivery discussion matters until this is read.

## Why this is now cheap and bounded

`init 0x9F3C` calls `run_bg_process("/usr/bin/bootin.elf")`, whose `0x90A4` body
forks then `execv(argv, envp)` with `envp` = a pointer to the argument string,
i.e. `*environ` unchanged. Only afterwards, at `0x9F6A`, does `main` blank
`LD_PRELOAD`. So on a genuinely fresh boot the child must inherit whatever
`/setting/env.txt` put there, and `bootin` in turn passes `__environ` to
`im.elf`. A single environ read therefore tests the whole chain.

## If it is a resumed boot

Then the delivery question becomes "how does the UI process come to exist",
not "which file carries the library". Candidates, in increasing cost:

1. Find the camera's own documented full-restart path that does not resume
   (the image only resumes when the bootloader is told to). Requires reading the
   bootloader side, not the kernel.
2. Force a cold path once, with an explicit, reversible precondition, and measure
   the environ again. `image_drop` zeroes the stored image header — that is a
   destructive write to persistent storage and must not be used casually; it is
   listed only as the mechanism that exists, not as a recommendation.
3. Accept the late-load route instead: keep the file-based delivery but drive it
   from a process that is not restored, and patch the live UI under quiescence.
   QEMU showed the lifecycle side works; the missing piece is the installer.

## If it is a fresh boot

Then the failure was measurement, and the next steps are already built:

1. Deploy the slim library to a **new** path (never overwrite a mapped file) and
   point `/etc/ld.so.preload` at it, with the previous list preserved on camera.
2. Reboot once, then read the new `im.elf`'s maps and `/log/newmenu.log`.
   The log now falls back `/log` -> `/tmp` -> `/etc` and prints `NM log=<path>`,
   so absence of output is no longer ambiguous with absence of loading.
3. Only if the library is mapped and no owned node appears, move to the installer
   step: the QEMU `OPEN_NATIVE` request plus the arm-on-consume gate.

## What not to do

- Do not call the object constructor or the initialization entry on a live view
  to "force" a rebuild. Static inspection shows `ViewSettingMenuToInstance`
  allocates a fresh 1192-byte object; calling it out of the manager's lifecycle
  would leak or corrupt ownership.
- Do not treat `NE_KEY_SENT` as proof of a reopen. One measured run showed the
  key was accepted and consumed as `native_action=15`, i.e. a native close, with
  no second initialization.
- Do not widen `ne_caller_may_insert` again. It is back to the single
  byte-verified Thumb return site; the generalized BL decoder was wrong for
  Thumb-tagged returns and indirect dispatch.
- Do not enable `NM_EXPERIMENTAL_NATIVE_PRESENT` as a late-load shortcut. It is
  initial-exec TLS and explicitly startup-only; the QEMU ARM fixture only covers
  mocked GPU boundaries.

## Immediate concretes

Camera is currently unreachable (no `PONG`, no Sony USB device), so the environ
read cannot be taken yet. While it is offline the useful work is:

- Prepare the exact environ-dump command and the interpretation table above as a
  one-shot script, so the next online window spends its time on measurement
  rather than on writing commands.
- Keep every prior artifact intact: `tools/zve10/backups/` holds the pre-change
  preload bytes, the stable 135332-byte library and the per-attempt manifests.

# Hardware ssboot status and the limits of the earlier timing claim

## Read-only hardware observations

On 2026-11-09, after relocating the existing deployed library to
`/usr/lib/libnewmenu_usr.so`, bounded agent RUN requests returned:

```text
/proc/ssboot/state       snapshot
/proc/ssboot/mode        optimize
/proc/ssboot/resmode     normal
/sys/power/image_mode   2
/sys/power/reboot_reason 0
boot_id                 6d5414d1-9c5a-4277-a841-f9ad2fab71f3
/proc/157/maps menu-library matches: 0
```

No reboot, UI restart, image-mode write, image-drop write, or snapshot regeneration
was performed. A newly exec'd utility mapped the verified library from `/usr` in
four segments on fc:0f. This proves current new-process loading, not menu behavior.

## Local source interpretation

Source inspected under the existing Sony kernel checkout:

- `include/linux/ssboot.h` identifies the facility as Snapshot Boot.
- `kernel/power/ssboot/proc.c:561` maps states to names and `state_show` prints
  the current `ssboot.state` at line 579.
- `kernel/power/ssboot/main.c:450` implements `ssboot_is_resumed()` as true for
  either `SSBOOT_STATE_SNAPSHOT` or `SSBOOT_STATE_RESUMED`.
- `kernel/power/ssboot/main.c:708` handles `SSBOOT_SSBOOT_PRE`/`SSBOOT_SSBOOT`
  by setting snapshot state, normal resume mode and optimize image mode.
- `kernel/power/ssboot/main.c:427` changes snapshot to resumed in
  `ssboot_finish()`, but the call in `kernel/power/suspend.c:420` is inside
  `#else !CONFIG_WARM_BOOT_IMAGE`. The warm-image path at lines 382–439 does
  not call it, so `snapshot` may remain visible while userspace is running.
- `arch/arm/mach-cxd900x0/resume.S:54–69` selects resume status, then restores
  saved execution context rather than starting a new executable. In
  `arch/arm/mach-cxd900x0/pm.c:237–251`, a successful disk-image resume invokes
  `ssboot_setmode()`.
- `kernel/power/suspend.c:455–463` thaws processes after resume. These paths
  support continuity of existing tasks; they do not identify a particular PID
  as a member of the saved image.
- `arch/arm/common/warm.c:113` defines `IMAGE_MODE_MAX` as 2. Its sysfs show
  routine at line 185 prints that configuration value. Therefore image_mode=2
  alone is not proof of a completed resume.

This source is locally available Sony code; the exact running camera kernel has
not been proved identical to this checkout. The matching runtime interface and
state meanings support the resume hypothesis without proving every detail.

## What is now a supported hypothesis

Early UI-chain processes may be restored from a saved startup image. If their
address spaces are restored rather than exec'd, a disk change to preload
configuration will not itself rerun the dynamic loader inside those processes.
Later exec'd utilities can still load the new library normally. This fits the
observed split and is a stronger candidate than the unmeasured late-mount claim.
It remains unconfirmed which user processes are in the restored image.

Changing a library's storage path to `/usr` does not by itself settle this issue:
the current file is byte-verified and loads for new utilities, while the current
UI process still has no mapping. Next validation must distinguish new exec from
image restoration and must preserve the owner's separate reboot authorization.

## Claims withdrawn

- Lack of fc:0a mappings proves `/etc` was not mounted: false inference.
- Process starttime gives its latest exec time: false; exec preserves creation time.
- PID 1 being launch_shell proves failed startup or that it launched bootin:
  not established by that observation.
- A new boot ID proves every userspace process reran the loader: not established
  on a system offering startup-image restoration.
- Eight unloadable-library tests in QEMU eliminate all boot risk on hardware:
  unsupported generalization; constructor failures, unresolved relocations,
  symbol interposition and UI-hook failures were not all exercised by those tests.

## Preserved deployment and next steps

The camera configuration currently preserves the agent entry and names
`/usr/lib/libnewmenu_usr.so` for the menu library. `/usr` was restored read-only.
The pre-relocation configuration is backed up on the camera at
`/etc/ld.so.preload.before-usr-20261109`, with complete local readbacks under
`tools/zve10/backups/usr-relocation-k9vduz_i/`.

Required remaining evidence: library mapping in the actual UI process; native
menu row and correct caption; ENTER opens the requested menu; MENU returns.
Neither snapshot settings nor image files should be modified merely on the basis
of these status reads. No task completion is claimed.

## Follow-up: preserved image and diagnostic boundaries

The camera returned `image_dev = *wbi4`, `resume_drop = 0`, `boot_drop = N`,
and `ignore_drop = Y`. Raw command and response are preserved locally in
`tools/zve10/backups/usr-relocation-k9vduz_i/ssboot-readonly.json`.
These are current configuration observations, not per-process exec history.

`arch/arm/common/warm.c:445–459` implements the image_drop write through
`_wbi_drop()`. At lines 364–405 the drop helper zeroes and writes the image's
first sector. Thus writing image_drop is a storage mutation, not a harmless
status query; no such write was performed.

Do not read `/proc/ssboot/section` as a passive probe: despite its read-only mode,
`kernel/power/ssboot/proc.c:368–435` freezes processes, allocates page bitmaps and
shrinks memory in its open callback. The supplied `probe_snapshot.py` avoids it,
uses one bounded RUN request, checks the fixed candidate PIDs' comm values and
also records a newly exec'd utility's mappings. Its syntax and client path were
validated and it was run successfully on the camera. The new utility mapped
both `/usr/lib/libnewmenu_usr.so` and `/etc/zve10_agent.so`; PIDs 1/149/157 did
not report either name. The boot ID remained unchanged, so no reboot is implied.

An independent read-only review of this source confirmed the state mapping,
warm-image completion-path distinction and lack of a direct per-PID membership
report. The mechanism is well supported; the exact UI-process history is still
unknown.

# What the a6400 repository gives us

Cloned to `/mnt/build/a6400/a6400` (792 KB). Same SoC family as the ZV-E10
(CXD90045, Cortex-A5), so several findings transfer even though the models differ.

## 1. `bk.elf` is the native Backup tool, and the ZV-E10 ships it too

The a6400 procedure for the 30-minute recording limit is:

```sh
/usr/bin/bk.elf r 003c0373     # read property
/usr/bin/bk.elf w 003c0373 0D  # write property
/usr/bin/bk.elf s              # sync
```

`bk.elf` is present in our own firmware at
`nflasha15_unpacked/bin/bk.elf` (10,088 bytes) and its strings confirm the
interface:

```
usage: bk command [command-option-and-arguments]
r        read backup data
w        write backup data
s        sync backup data
x        read reset backup data
```

It links against `libbackup.so` and calls `Backup_read`, `Backup_write`,
`Backup_setting_attr`, `Backup_sync_all`.

**Why this matters for us:** we already proved `libbackup.so` is callable from
the agent (after preloading `libosal_utm`, `libosal_uipc`, `libosal_ulogio`).
`bk.elf` is the same API behind a command line, so the three Wi-Fi properties can
be read and written with a single on-camera command instead of a custom binary.
That removes the PMCA/Jiritsu dependency entirely for this step.

## 2. Their boot persistence is `DT_INIT` injection, not a new file

`hook/build_autoexec_hook.py` patches an unused library (`libAK8975.so`, an AKM
compass driver with no magnetometer on this body) by:

- overwriting the start of `.text` (`0x988`) with a hand-assembled ARM
  constructor, and
- rewriting the `.dynamic` array's `DT_NULL` terminator into
  `DT_INIT = 0x988` plus a fresh `DT_NULL`.

The constructor double-forks, `setsid()`, and `execve`s `/bin/sh /tmp/autoexec.sh`.

**This is the cleanest delivery mechanism we have seen for this platform.** It
needs no `im.elf` replacement, no cold boot, and no WBI manipulation: the library
is loaded by `/etc/ld.so.preload` into every new process, and the constructor
runs at load time. On a snapshot-resumed system `/etc/ld.so.preload` still does
not reach `im.elf`, so it does not solve the UI-process problem directly — but it
is a guaranteed-execution primitive for any process that *is* freshly exec'd.

Note the a6400's own warning: their `/tmp` is persistent flash, so a stale
`O_EXCL` lock would silently disable the hook on later boots; they add a trap to
remove it on exit. Our `/tmp` is `nflasha10` vfat, also persistent, so the same
trap is required.

## 3. On-screen GUI is a known dead end there too

Their `gui/` is a freestanding framebuffer program (~25 KB, raw syscalls, no
libc), but the README states the display is driven by a proprietary DMP SUGILITE
GPU with no `/dev/fb0`, and that menu files are binary `.uxc` on a read-only
partition with no repack tools. They also list "on-screen GUI / menu
modifications" under confirmed dead ends.

**That is the same wall we hit.** Our ZV-E10 route (native menu injection into
`viewUnified2.so`) is more advanced than anything in this repo; it is not a
source of new menu-injection technique.

## 4. Two useful incidental facts

- `PMCA pull` truncates every file by exactly 32 KiB; they compensate by
  chunking with `dd`. We already chunk our pull/push paths, so no change needed.
- Physical key codes for the a6400 are decoded from `BIN!` block 0x1 at
  Backup.bin offset `0xc67f4` and `libObj.so` at `0x13e18cc`, and the note that
  codes skip `0x1A..0x1F` is a trap worth remembering if we ever derive key codes
  by counting strings.

## What to actually use

The `bk.elf` interface. It is the smallest, most native way to touch the three
Wi-Fi Backup properties, and it is already on the camera.

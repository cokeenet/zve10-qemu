# /etc/ld.so.preload works on a vfat /etc, including for im.elf

## Setup

QEMU init mounts a FAT16 image directly onto `/etc` before starting anything:

```sh
mount -t vfat -o loop /etc.img /etc
```

That image contains `ld.so.preload` holding `/etc/mini_preload.so`, plus a
minimal library whose constructor prints its own pid. `/usr/bin/im.elf` is
replaced with a program that dumps mounts, paths and environ.

## Result

```
ETC_DIRECT_MOUNT_OK
/dev/loop1 /etc vfat rw,relatime,fmask=0022,dmask=0022,codepage=cp437,
                    iocharset=ascii,shortname=mixed,avoid_dlink,errors=remount-ro 0 0

MINIPRELOAD: loaded into pid=67
MINIPRELOAD: loaded into pid=68
...
MINIPRELOAD: loaded into pid=133
========== FAKE im.elf (mount timing probe) ==========
```

89 processes loaded the library. **pid 133 is im.elf itself**, and the print
appears immediately before its own output, so the constructor ran inside it
before main.

## Why the earlier attempt failed

The first try used `mount --bind /etc_vfat /etc` and produced:

```
ERROR: ld.so: object '/etc/testpreload.so' from /etc/ld.so.preload cannot be preloaded: ignored.
```

That message named the *old* content of the initramfs `/etc/ld.so.preload`, not
the vfat one -- the bind did not take effect for the loader's view. A direct
`mount` onto `/etc` does, and that is what the camera does too (`init` at
`0x9B38` mounts nflasha10 at `/etc`).

## What this settles

`/etc/ld.so.preload` is a viable delivery path on this hardware. vfat supports
it, glibc reads it there, and it applies to the UI process.

The earlier conclusion that "im.elf starts too early for /etc" is **wrong**.
On hardware `/etc` is mounted at `0x9B38` and im.elf does not appear until
tick 104; there is a wide margin. The QEMU run here reproduces a vfat `/etc`
and shows the loader honouring the file for im.elf.

So on hardware either:

- `/etc/ld.so.preload` did not contain what we thought at boot time, or
- the library it named failed to load (missing / wrong ABI / crash in its
  constructor and glibc recovering), or
- the process we inspected was not started through the path we assumed.

The second is worth ruling out cheaply and has a concrete test: put a library
in `/etc/ld.so.preload` that only prints one line and exits, then check
whether that line appears. If it does, the mechanism works and the problem is
the menu library; if it does not, the problem is the file or the mount.

## Reproduction assets

- `tools/zve10/qemu/etc.img` -- FAT16 with `ld.so.preload`, `mini_preload.so`,
  `toprc`
- `tools/zve10/qemu/mount-timing/fake_im.c` -- the probe that dumps mounts,
  paths and environ from inside im.elf
- `tools/zve10/qemu/run.sh` -- starts each experiment as its own systemd unit
  with a memory cap and always stops it again

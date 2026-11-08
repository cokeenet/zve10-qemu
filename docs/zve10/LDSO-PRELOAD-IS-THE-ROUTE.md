# /etc/ld.so.preload is the delivery route: proven end to end

## Setup

Instead of `/setting/env.txt`, mount a FAT16 image directly onto `/etc` and put
the real `libnewmenu.so` there with a one-line `ld.so.preload`:

```
/etc/ld.so.preload   ->  /etc/libnewmenu.so
/etc/libnewmenu.so   ->  libnewmenu.so, 135,428 B, md5 fa82a02b
```

No `LD_PRELOAD` anywhere in the environment. The init script does `unset
LD_PRELOAD` at the top and starts bootin without any preload assignment.

## Result

Every process on the boot path loads it:

```
NM === boot pid=63  exe=?                    NM LD_PRELOAD=<unset>  NM /etc/ld.so.preload readable=1
NM === boot pid=67  exe=/bin/busybox         NM LD_PRELOAD=<unset>  NM /etc/ld.so.preload readable=1
...
NM === boot pid=126 exe=/usr/bin/im.elf      NM LD_PRELOAD=<unset>  NM /etc/ld.so.preload readable=1
NM ui pid=126 lock=1
NM waiting for dlopen (no timer, hook-driven)
NM native_dlopen resolved
NM dlopen path=libosal_uipc.so flags=0x101 -> 0x40ae548
NM dlopen path=libosal_utm.so  flags=0x101 -> 0x40ae2a0
NM dlopen path=libbackup.so    flags=0x101 -> 0x40ae000
```

`LD_PRELOAD=<unset>` and still loaded: the library arrived through
`ld.so.preload`, which is the loader's own file, not the environment.

## Why this is better than the env.txt route

| | `/setting/env.txt` | `/etc/ld.so.preload` |
|---|---|---|
| depends on init running `set_oreore_env` | yes | no |
| depends on env being inherited by bootin | yes | no |
| depends on `/setting` being mounted in time | yes | no |
| needs a dmode gate to be open | yes | no |
| touched files | `env.txt` + library | `ld.so.preload` + library |

The env.txt route has four things that must all hold; this one has none of
them. It only needs `/etc` mounted before im.elf starts, which init does at
`0x9B38` versus im.elf at tick 104.

## Why previous attempts on hardware may have failed anyway

The QEMU run before this one used `mount --bind` rather than a real mount and
the loader kept seeing the initramfs `/etc`, which is why it reported the old
path. A direct mount, as done here, works. On hardware the guest does a real
mount at `0x9B38`, so this is not the failure mode there -- but it is worth
knowing that `--bind` is not a faithful imitation.

## Next steps

1. Rebuild the hardware package around this route: `ld.so.preload` naming the
   library, library beside it, everything under `/etc`.
2. Keep the env.txt route as a fallback but stop treating it as primary.
3. The remaining QEMU gap is unchanged: CP init stops at `entry_main` with 22
   missing host APIs, so `viewUnified2.so` never loads and the hooks never get
   their `views_base`. That is the thing standing between "library loads in the
   UI process" and "a new menu row appears".

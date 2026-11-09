# Decisive measurement: env.txt is parsed fine, so the loss is after putenv

## Measured on the camera, current boot

Boot id `ce0db914-14a2-4e0a-8d02-a4cb428ccda9`, uptime ~1800 s. PID 1 is
`launch_shell.elf`. `/proc/149` is `bootin.elf` (starttime 97) and `/proc/157`
is `im.elf` (starttime 104), both with zero `libnewmenu` mappings.

`/setting/env.txt` is 25 bytes, exactly:

```
4c44 5f50 5245 4c4f 4144 3d2f 6574 632f   LD_PRELOAD=/etc/
6e6d 6c69 622e 736f 0a                     nmlib.so.
```

### The probe

An 8,752-byte ARM binary replicating `set_oreore_env` exactly — `open`, `fstat`,
`mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)`, copy, `putenv` — was uploaded,
hash-verified by readback, executed, then removed.

Output:

```
open=6 errno=0
fstat=0 size=25
mmap_ok=0x4ca2000
bytes:4c445f5052454c4f41443d2f6574632f6e6d6c69622e736f0a
putenv_ret=0
getenv_LD_PRELOAD=[/etc/nmlib.so
]
```

**Every step of the previously suspected failure path succeeds on the camera,
on the camera's own filesystem, with the camera's glibc.** The mmap-on-vfat
hypothesis is dead, and so are the `open` and `st_size` branches. `putenv`
returns 0 and the variable is readable afterwards.

### Direct comparison of the two environ blobs

```
/proc/1/environ     ... LT_ABORTREASON=0 \0 LD_PRELOAD= \0
/proc/149/environ   ... LT_ABORTREASON=0 \0            (absent)
```

PID 1 carries the empty value and `bootin` does not carry the name at all. That
is the opposite of a failed parse: the line was read and applied, then blanked.

## Where the blanking actually happens

`init`'s `main` does two distinct env operations:

- `0x9B82` — `set_oreore_env()` (return ignored, no branch on it)
- `0x9F6A` — `putenv("LD_PRELOAD=")` — literal at `0xA024`, guarded only by the
  flag at `0x14868`

The `bootin` launch happens **between** them:

- `0x9F3C` — `run_bg_process("/usr/bin/bootin.elf")`
- `run_bg_process` at `0x90A4`: `fork`, then `execv(argv, envp)` at `0x90C2`
  with `envp = &argv[0]` — i.e. inherited `environ`, unchanged.

So the child is created while `LD_PRELOAD` is still set, and `main` blanks the
variable afterwards in its own copy. That ordering explains why PID 1 shows the
empty value.

It does **not** explain why `bootin` — created earlier — lacks the name entirely.
Two candidate causes remain, and they are distinguishable:

1. **This boot did not run `/sbin/init`'s path at all** (startup-image resume), so
   the `bootin` we see at starttime 97 was restored with its snapshot-time
   environment and never saw the newer `env.txt` line.
2. `/setting/env.txt` did not exist yet at `0x9B82` on this boot, i.e.
   `/setting` was not mounted in time and `set_oreore_env` took the silent
   `open` failure branch — even though the file exists now.

Candidate 2 is checkable without rebooting: `init`'s `0x9B82` runs **after**
`/setting` is mounted at `0x9B56`, and `mount_partition` at `0x9244` does a
`statfs`, refuses mounts above 204800 blocks, and otherwise umount/remounts. If
`/setting` were late, every other setting read would fail too, and `dmode=3` is
in force — so candidate 2 is weak but not excluded.

Candidate 1 is supported by the earlier snapshot reads
(`ssboot/state=snapshot`, `resmode=normal`, `image_dev=*wbi4`).

## Consequence for the next step

The delivery question is no longer "does the file work". It is **"does this boot
execute `/sbin/init` with `/setting` mounted before `0x9B82`"**. A fresh, non-resumed
boot would answer both candidates in one shot:

- `bootin` environ containing `LD_PRELOAD=/etc/nmlib.so` → resume was the cause,
  and the file route works on a cold boot.
- still absent → the loss is upstream of `putenv`, inside `init`'s own ordering,
  and no file-based route can fix it without changing `init`.

## Housekeeping

The probe binary was deleted from `/tmp` after the run and `sync` issued. No
file on a persistent partition was written by this measurement; `env.txt` and
`/etc/ld.so.preload` were read only. No reboot was performed.

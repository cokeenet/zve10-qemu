# Probe result: the env.txt chain works, on vfat too

## What was tested

A replica of init's chain, compiled for ARM and run inside QEMU, first against
an initramfs directory and then against a real FAT16 image loop-mounted at
`/setting`.

The replica follows the disassembly exactly:

```
check_mode_file()      open /setting/mode/dmode, then set flag = 1
gate                   if (flag == 0) skip
set_oreore_env()       open /setting/env.txt
                       fstat -> size
                       if (size == 0) return
                       mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)
                       if (MAP_FAILED) return
                       parse_and_set_env(buf, size)
                       munmap, close
```

## Result: both paths succeed

```
VFAT_MOUNT_OK
-rwxr-xr-x    25  env.txt
drwxr-xr-x        mode/

=== probe 直接读 vfat 路径 ===
  [check_mode_file] dmode="3" (read 1 B) -> flag=1
  flag==1 -> 调用 set_oreore_env
  [set_oreore_env] st_size=25
  [set_oreore_env] applied 1 vars
  LD_PRELOAD = /etc/nmlib.so
VFAT_PROBE_EXIT=0

=== probe 读 initramfs 路径（对照）===
  ... identical ...
  LD_PRELOAD = /etc/nmlib.so
PROBE_EXIT=0
```

So `mmap(..., MAP_SHARED, ...)` on vfat is fine, `parse_and_set_env` handles
the line, and `putenv` takes effect. The earlier guess that `mmap` was failing
on vfat is **wrong**.

## What this rules out

| candidate | status |
|---|---|
| `check_mode_file` gate closed | ruled out -- dmode holds `3`, flag becomes 1 |
| `open("/setting/env.txt")` failing | ruled out |
| `st_size == 0` | ruled out |
| `mmap(MAP_SHARED)` failing on vfat | **ruled out by this test** |
| `parse_and_set_env` mishandling the line | ruled out |
| bootin's `flag & 0x40` unsetting it | ruled out -- needs an argument, init passes none |
| DUMA block overwriting it | ruled out -- needs `d`/`D` in dmode, it holds `3` |
| agent library clearing the environment | ruled out -- constructor only forks and binds a socket |

Every link in the static chain now has either a disassembly citation or a
runtime test behind it, and the hardware still did not load the library.

## The remaining anomaly

On hardware, `im.elf` is pid 157 with **ppid 1**. bootin does `fork()` then
`execve()` then `waitpid()` in a loop (`0x9002`-`0x9020`), so something that
expects to be the parent is waiting -- yet im.elf ended up reparented to init.

That suggests the process that starts im.elf on hardware is **not** the bootin
invocation init performs at `0x9F3C`, or that bootin exits before the child
execs and something else restarts im.elf. Either would explain how the
environment we set in init never reaches the im.elf we observed.

This is the next thing to check with hardware access: find out what actually
spawns pid 157. `dmesg` may name a supervisor, or `bootin`'s own log may show
whether its child survived.

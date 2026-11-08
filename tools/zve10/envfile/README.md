# /setting/env.txt — the vendor's own environment injection point

## What it is

`init` contains a function `set_oreore_env` at `0x98B0`:

```c
set_oreore_env(void)
{
    int fd = open("/setting/env.txt", O_RDONLY);
    if (fd < 0) return 0;                     /* file absent: silent no-op */

    struct stat st;
    fstat(fd, &st);
    if (st.st_size == 0) { close(fd); return 0; }

    void *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    parse_and_set_env(p, st.st_size);
    close(fd);
    return 0;
}
```

`parse_and_set_env` at `0x97CC` walks the buffer line by line. For each
`KEY=VALUE` line it `malloc`s a copy and calls `putenv`; a line with no `=`
becomes `unsetenv`. Comments and blank lines are skipped by
`skip_comment_and_space`.

## Why this matters

It runs **before** `bootin` is started, and everything `init` puts in its own
environment is inherited down the chain:

```
0x9B56  mount /setting
0x9B78  check_oreore2()
0x9B82  set_oreore_env()        <- reads /setting/env.txt
...
0x9F3C  run_bg_process("/usr/bin/bootin.elf")
          -> execv() inherits init's environ
        bootin
          -> execve(im_elf, argv, *environ)   (see docs/zve10/BOOT-CHAIN-FACTS.md)
        im.elf with whatever LD_PRELOAD we set
```

So a plain text file on a writable vfat partition (`/setting`, nflasha2) is
enough to get a library into `im.elf`. No initrd patching, no firmware file
replacement, nothing that can brick the camera.

The file does not exist on a stock camera, which is why nobody noticed it.

## Contents

```
LD_PRELOAD=/etc/libnewmenu.so
```

One line. `parse_and_set_env` uses `putenv`, so the value is live for the rest
of the boot.

## Deploy

```
# 1. push the library somewhere persistent that init can see
#    /etc is vfat (nflasha10) and survives reboots
chunk_push.py libnewmenu.so /etc/libnewmenu.so

# 2. create /setting/env.txt
chunk_push.py env.txt /setting/env.txt

# 3. reboot
```

## Rollback

```
rm /setting/env.txt
reboot
```

Since `set_oreore_env` treats a missing file as a no-op, deleting it restores
stock behaviour exactly. **This is the safest injection route found so far** —
nothing on the boot path is modified.

## Caveats

- `parse_and_set_env` caps each line at 1024 bytes (`cmp r9, #0x400`).
- It allocates with `malloc`, not a stack buffer, so the value stays valid
  after the function returns.
- The file is read once, at init time. Changing it takes effect on the next
  boot.


## The gate in front of it: /setting/mode/dmode

`set_oreore_env` is not called unconditionally. main does:

```
0x9B78  bl check_oreore2        -> check_mode_file
0x9B7C  ldr r3, [0x9D60]        -> .bss flag at 0x14868
0x9B7E  ldr r3, [r3]
0x9B80  cbz r3, 0x9B86          -> skip if zero
0x9B82  bl set_oreore_env
```

`check_mode_file` at `0x93B0` sets that flag to 1 at `0x9404`, but only after
`open("/setting/mode/dmode")` succeeds at `0x93CC` (path literal `0x0A5BD`).
If the open fails it jumps to `0x947C`, which returns -1 without touching the
flag -- and then `.bss` is still zero and `set_oreore_env` is skipped
entirely.

On a normal camera `/setting/mode/dmode` exists and holds `3`, so the gate is
open. `deploy.sh` checks for it anyway and creates it with `3` if it is
missing, since losing the env.txt injection to a missing two-byte file would
be a confusing way to fail.

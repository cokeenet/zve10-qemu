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

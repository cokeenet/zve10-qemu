# Library blast radius: why the menu library cannot cause total USB silence

## The symptom being explained

After the Sony `ud_wbi_drop` invalidation and a `fastboot reboot`, the camera
showed no USB device at all: no `054c:0996`, no `054c:0994`, no `18d1:4ee0`.

## What the library is allowed to touch

`nm_boot` is the only constructor. It runs this and nothing else:

```c
if (!nm_is_ui_process()) return;      /* every process except im.elf exits here */
dl_iterate_phdr(nm_phdr, NULL);       /* read-only walk */
nm_try_install();                     /* returns immediately if !object_base */
```

`nm_phdr` records `object_base` and `views_base`. Its only call with a side
effect is `dlopen(path, RTLD_NOLOAD | RTLD_LAZY)`, which loads nothing: with
`RTLD_NOLOAD` it returns NULL unless the library is already mapped.

`nm_try_install` writes to exactly four addresses, all inside the UI process's
user-space text:

```
caution   + 0xb8dffa   CautionConfig.so   node ctor (row insertion point)
views     + 0x3014f0   viewUnified2.so    populate
views     + 0x2fccc0   viewUnified2.so    main/input
views     + 0x862130   viewUnified2.so    row draw
object    + 0x626538   libObj.so          string-id entry
object    + 0x6266d0   libObj.so          string-chars entry
```

Each write is guarded by a 12-byte `memcmp` against the expected original code,
so a wrong offset aborts instead of corrupting. The write itself goes through
`mprotect` on the containing page(s) only.

## Why USB enumeration is outside that range

USB is provided by the kernel's dwc3 driver, which is up before `/sbin/init`
executes. The process chain is:

```
kernel + dwc3      (USB already enumerable here)
/sbin/init (1)
  └─ bootin.elf (149)      started at init 0x9F3C
       └─ im.elf (157)     execve from bootin 0x9F54
```

`im.elf` is the third level. Even if it died instantly, `bootin` would reap it
via `waitpid` at 0x9F6A and the kernel would keep enumerating USB. The library
never runs in `init`, `bootin`, or `launch_shell` at all.

## Empirical confirmation from this camera

Two earlier boots in this session, both with the library present in
`/etc/ld.so.preload`:

| boot_id | libnewmenu maps in im.elf | USB enumeration |
|---|---|---|
| 2a94d022-… | 0 | normal (`054c:096f`) |
| 16173377-… | 0 | normal (`054c:096f`) |

The library had never actually loaded into `im.elf` on either boot, and the
camera was fine. The only change before the failure was the WBI header, not the
library.

## What actually broke it

A soft restart through `fastboot reboot` runs the suspend path. `warm.c`
contains:

```c
if (wbi_chk_opened_files()) {
    wb_printk("some files are still opened, stopping all of operations\n");
    _wbi_drop();
    while (1) ;
}
```

With files open at shutdown this spins forever instead of completing the power
transition. Combined with an already-invalidated WBI header, the camera never
reaches a clean cold boot.

## Conclusion

The library is not the cause and cannot be: its entire effect is six guarded
writes inside `im.elf`'s own text. Recovery is a matter of restoring the WBI
header and then power-cycling by removing the battery, never by a soft reboot.

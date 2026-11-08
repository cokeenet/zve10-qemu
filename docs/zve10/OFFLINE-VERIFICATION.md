# Offline verification status

Everything below was checked without the camera. What is left needs hardware.

## Delivery path: /setting/env.txt

**Static, from disassembly of the initramfs init**
(md5 `0ff6dd725d90ebd67ecc44839be58f3f`, 28,580 B)

| Step | Address | Evidence |
|---|---|---|
| mount /setting | `0x9B56` | before the env read |
| check_mode_file opens `/setting/mode/dmode` | `0x93CC`, path at `0x0A5BD` | gate for the next step |
| sets flag `0x14868` = 1 | `0x9404` | only on a successful open |
| main tests that flag | `0x9B7E` / `0x9B80` | skips the call when zero |
| set_oreore_env call | `0x9B82` | |
| opens `/setting/env.txt` | `0x98BA`, path at `0x0A77A` | missing file returns 0 |
| mmap PROT_READ MAP_SHARED | `0x98D0`-`0x98E0` | vfat-compatible, read only |
| parse_and_set_env | `0x9910` → `0x97CC` | |
| memchr '=' decides putenv vs unsetenv | `0x9854` / `0x985C` / `0x9864` | |
| copy is malloc'd | `0x981C` | so the value outlives the call |
| bootin starts | `0x9F3C` | after all of the above |
| bootin execs with envp = *environ | `bootin 0x8FF2`, literal `0x905C` → `0x13628` | |
| bootin leaves LD_PRELOAD alone | flag test at `0x9B24`, `argc == 1` | |
| bootin only mounts /dev/pts | `0x92EE` | does not disturb /etc |

**Dynamic, in QEMU**

The QEMU init is a shell script, so it was changed to do what
`set_oreore_env` does, and the hardcoded LD_PRELOAD was removed so the file is
the only source. With `LD_PRELOAD=/z.so` in `/setting/env.txt`:

```
ENVTEST: found /setting/env.txt
ENVTEST: applying LD_PRELOAD=/z.so
NM === boot pid=117 exe=/usr/bin/bootin.elf ===   NM LD_PRELOAD=/z.so
NM === boot pid=127 exe=/usr/bin/im.elf ===       NM LD_PRELOAD=/z.so
```

31 processes, including im.elf. libObj matched 21 times,
`object_base=0x9adc000` each time.

Details: `docs/qemu-verification/ENVTXT-END-TO-END.md`

## The library

| Check | Result |
|---|---|
| exports `dlopen@@GLIBC_2.4` | yes, 888 B implementation |
| NEEDED: libdl, libpthread, librt, libc | all present in the camera's /lib |
| target symbols | libObj.so, viewUnified2.so, CautionConfig.so |
| size / md5 | 135,304 B / `d4b28630d725f65d6ac851aed9900c2c` |

**dlopen override is not shadowed.** None of libObj.so, CautionConfig.so,
viewUnified2.so, libIMDB.so or libbackup.so exports dlopen, and the reference
firmware has no argtrace.so, so libdl-2.11.2.so is the only provider. QEMU
confirms the override fires for every call.

**Native dlopen resolution has four fallbacks** (dlvsym, dlsym, libdl handle via
RTLD_NOLOAD, `__libc_dlopen_mode` from libc), because returning NULL there
would break every dlopen in im.elf.

**No timer.** An earlier setitimer(ITIMER_REAL) at 500 ms injected SIGALRM into
im.elf's main thread and froze the menu. Retries now piggyback on each dlopen
instead.

## Hook anchors, verified against the shipped libraries

```
libObj.so      @ 0x626538   90 b5 83 b0 00 af 04 46
libObj.so      @ 0x6266d0   f0 b5 00 f1 98 06 85 b0
CautionConfig  @ 0xb8dffa   03 68 2d e9 f0 41 00 af  d3 f8 e8 30
```

The CautionConfig anchor symbol is `GLOBAL DEFAULT` at `st_value 0xb8eb21` and
every dlopen in the boot uses flags `0x101` (RTLD_LAZY|RTLD_GLOBAL), so
`dlsym(RTLD_DEFAULT)` will find it once loaded.

`CautionConfig.so` arrives as a `DT_NEEDED` of `viewUnified2.so`, not through a
dlopen of its own, which is why `nm_try_install` is retried on every dlopen
call rather than only on matching paths.

## env.txt format

Reimplemented `parse_and_set_env` in `tools/zve10/envfile/test/` and checked six
shapes: leading comment, leading spaces, CRLF, two variables, no trailing
newline, empty file. All behave as the binary would.

## Only hardware can answer

- the real `check_mode_file` gate against the camera's actual dmode file
- whether `/etc` and `/setting` are mounted the way the disassembly says
- whether viewUnified2.so is loaded at all in a real UI session (QEMU never
  reaches it)
- whether the hooks produce a menu entry

## Hook machinery

`ne_hook` patches 8 or 10 bytes depending on the target's alignment, and
`ne_jump` produces exactly that many:

```c
ne_jump(where, target):
    literal = (where + 7) & ~3;
    pc      = (where + 4) & ~3;
    LDR.W PC, [PC, #(literal - pc)]     ; 4 bytes at where
    if (literal != where + 4) NOP       ; 2 bytes, alignment fix-up
    *(uint32_t *)literal = target       ; 4 bytes
    return literal + 4 - where;
```

| hook | address | align | bytes reserved | bytes written |
|---|---|---|---|---|
| CautionConfig init | `0xB8DFFA` | 2 | 10 | 10 |
| viewUnified2 populate | `0x3014F0` | 0 | 8 | 8 |
| viewUnified2 main | `0x2FCCC0` | 0 | 8 | 8 |
| viewUnified2 row | `0x862130` | 0 | 8 | 8 |

The immediate is 0 for the aligned cases and 4 for the misaligned one, well
inside the LDR.W literal range. The original 12 bytes are copied to a
trampoline that ends with a jump back to `address + 12`, so the patched
prologue is not lost.

## Where the entry actually gets added

`ne_init_wrapper` only appends a row when the caller is `views_base +
0x301921`, so it depends on viewUnified2.so being loaded. On this firmware
`libObj.so` is the only thing carrying the string `viewUnified2`, so the chain
is:

```
libObj.so loads            -> object_base known
user opens the menu        -> libObj dlopens viewUnified2.so
our dlopen hook sees it    -> views_base known, 4 hooks installed
CautionConfig.so arrives   -> (as a DT_NEEDED of viewUnified2)
ne_init_wrapper fires      -> ne_add_row
```

`ne_populate_wrapper` then asserts the page has exactly 6 entries and the new
node is at index 6; if the menu layout differs it logs
`NE_ERROR phase=initialized_path` rather than corrupting anything.

This is why nothing happens at boot: the entry appears when the menu is built,
not when the library loads.

## libObj.so really does call dlopen, and the override wins

`libObj.so` carries one `R_ARM_JUMP_SLOT` relocation at `0x013D9A44` against
the undefined symbol `dlopen`, so its `dlopen@plt` calls go through the
dynamic linker rather than being resolved at link time. That is the entry
point the override needs.

The override is chosen, not just present:

| | symbol | size |
|---|---|---|
| libnewmenu.so | `dlopen@@GLIBC_2.4` | 888 B (real implementation) |
| libdl.so.2 | `dlopen@@GLIBC_2.4` | 60 B (thunk) |

Both are the default version of `GLIBC_2.4`, which is what libObj.so asks
for, and `LD_PRELOAD` places libnewmenu.so ahead of libdl in the global
scope, so the first definition found is ours.

The string `viewUnified2.so` sits at `vaddr 0xE4D402` in libObj.so, directly
followed by `initializeViewConfig`, which is consistent with the view library
being opened from that code path. No literal in the file points at that
address, so the reference is PC-relative or computed, which is why the
override matching on the path string is more robust than hooking a fixed
call site.

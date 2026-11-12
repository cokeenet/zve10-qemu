# What the A7M3 repository settles about our problem

Cloned to `/mnt/build/sony_a7m3/Sony_A7M3` (3.4 MB, depth 20). It targets
ILCE-7M3 4.04 (`37114_13.2026020901`) and has no ZV-E10 support, but its
documentation answers three questions we were guessing about.

## 1. It confirms our `/usr` observation exactly, from the other side

`research/native_install_notes.md` reports that after writing new files and
renaming directory entries, a cold boot brought back **old inode numbers** in the
VFS while the raw directory held new ones:

> raw 目录使用新 inode 16687/16688，VFS 却恢复旧 inode 16523/16411

and `docs/native-creative-look.md` records the remedy they used:

> 按原厂升级步骤使旧 WBI 头失效后，用户断电重启，新目录与两份完整语言资源校验均通过

So on A7M3 the same class of problem exists and is solved by **invalidating the
WBI header and then power-cycling**. That is exactly the mechanism we identified
independently on ZV-E10: the warm-boot snapshot restores stale page cache and
stale directory state, and clearing the header is what forces a real cold boot.

Our earlier measurement — that `/usr/bin/im.elf.real` survived while the
overwritten `/usr/bin/im.elf` reverted — is the same phenomenon seen from the
other direction: a **new** inode's contents are not in the snapshot, so they
survive; an **existing** inode's old contents are, so they win.

## 2. It confirms that `cldboot` is not a "WBI is bad" fallback

Their invalidation routine (`research/native_menu_wbi.c`, built by
`research/native_menu_wbi.py`, bound by `native_menu_wbi_bind.py`) is careful to
zero **only the first 512 bytes** of `/dev/nflasha5` and then verify the
neighbouring 512 are unchanged. That is the same operation we performed, and they
pair it with a **power cycle**, never with a software reboot.

That matches what we measured in Loader2 on ZV-E10: `cldboot`'s handler at
`0xFE049A43` is preceded by `0xFE049A42: movs r0,#0 ; bx lr`, i.e. the table's
"cold boot" entry evaluates to zero for every header state. The boot mode is
decided by hardware and the predicate table, not by "the WBI was invalid".

## 3. The most important transferable idea: patch a loader seam, not im.elf

This is the design we should copy. `research/native_menu_loader.S` is 60 lines of
Thumb assembly that:

1. sits in **already-reserved NOP caves** in the RW image (`0x5b9a7c` 52 bytes and
   `0x5b9b1c` 82 bytes) — it steals no real code;
2. replaces one 4-byte call at `0x20d068` (the original ViewGlobalMenu
   constructor call site);
3. calls the original `dlopen` PLT (`0x15d958`) for `/usr/lib/a7fl.so`, then the
   original `dlsym` PLT (`0x15c8dc`) for `a7_fl_install`;
4. every path restores its arguments and tail-calls the original constructor.

Their patch builder (`research/native_menu_loader.py`) proves bytewise that the
edited image differs **only** inside those three reserved ranges, and refuses to
run if the original hook bytes changed.

This route needs no `LD_PRELOAD` at all, and no `im.elf` replacement. It hooks the
library that already gets dlopen'd during normal UI startup.

## 4. Their file staging discipline, which we should adopt wholesale

From `native_install_notes.md`, in order:

1. remount the exact `/usr` ext2 writable, re-read mounts independently;
2. create candidates with `O_CREAT|O_EXCL`, write completely, `fsync`, reopen and
   hash-verify;
3. **hardlink** the original file as a backup (`.a7fl-*.orig`), then `rename`
   the candidate over the target, `fsync` the directory;
4. verify, flush, remount read-only, and independently confirm it;
5. write a journal on `/setting` first, and refuse to resume blindly.

They also state the constraint that applies to us: the view file is 10,854,816
bytes on a 314,572,800-byte partition with only 9,567 free blocks of 1,024 bytes,
so a **full copy-on-write replacement of `viewUnified2.so` cannot fit** (it needs
11,386 blocks, short by 1,819). Their answer is tiny in-place cave writes. Ours is
the same: the four hook sites we already pin are single small writes.

## 5. What does not transfer

- **No ZV-E10 build ID, model property, or library hashes.** Every pin in that
  repo is `ILCE-7M3` / `37114_13.2026020901` / its own `viewUnified2.so` SHA.
- **The menu integration is A7M3-specific**: page 14 of Camera Settings 1,
  `viewUnified6.so` writable-data vtable replacement, `0x5944ac` row-fill.
- **Their USB maintenance channel is macOS UTM + Sony-PMCA-RE.** Ours is a Pico
  on the ZV-E10's UART into BootROM, which is a different entry point entirely.

## What this changes for us

Their approach says: do not fight the snapshot by replacing a file the snapshot
contains. **Patch a seam inside a file, using reserved space, and let the
snapshot restore it as part of its normal content.** A cave write changes bytes
inside an inode the snapshot already tracks, so it survives the same way any
other legitimate change does — and their own cold-boot acceptance shows it works.

That is a better plan than the `im.elf` wrapper, because it needs no new file,
no bind mount, and no `LD_PRELOAD`.

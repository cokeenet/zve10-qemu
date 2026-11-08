# The CP blocker, narrowed

## What was tried

Matching the upstream launcher more closely:

| parameter | ours (before) | upstream | effect |
|---|---|---|---|
| `-smp` | 4 | 1 | **smp=1 panics** the kernel in `proc_readdir_de` |
| `cfg.uart` | 3 | 1 | no change |
| `loglevel` | 8 | 5 | no change |
| `-initrd` | no | yes | rejected: initrd overlaps reserved memory |
| kernel | kbuild-ui2 | kbuild-ui | upstream's tree is not present |
| initramfs | 300 MB embedded | 252 MB embedded | trimmed ours to 245 MB |

## smp=1 is not usable here

With `-smp 1` the kernel dies while `ps` walks `/proc`:

```
PC is at ida_remove+0x7c/0x10c
Backtrace:
  ida_remove  <- pde_put
  pde_put     <- proc_readdir_de
  proc_readdir_de <- proc_readdir
  proc_readdir    <- proc_root_readdir
  ...
  Process ps (pid: 122)
```

That is inside procfs teardown of a directory entry, and it does not happen with
`-smp 4`. Upstream runs `-smp 1` against a different kernel tree
(`work/kbuild-ui`), which is not present in this workspace, so the comparison
cannot be completed without rebuilding that tree.

## -initrd is not usable here

```
INITRD: 0x122c7000+0x11e28e00 overlaps in-use memory region - disabling initrd
```

The cpio was 300,060,160 B. Trimmed to 245 MB and embedded again; below is what
the embedded path does.

## Where the CP stops, with the trimmed image and smp=4

```
CP 加载:      1
NULL 数:      22
entry_main:   2
viewUnified2: 0
```

The 22 missing host APIs, by index:

```
130 131 132 133 134 135 136 137 138 139 140 141 142 143
198 199
203 204 205 206 207 208
```

Indices 130-143 are a contiguous run of fourteen, which suggests one service
group rather than scattered gaps. `liro.ko` exports 331 symbol names, but the
index order is that of its internal OSAL table, not the string order, so
mapping index to name needs the table itself -- `.data` is only 0x188 bytes and
`.bss` 0x1e7c, so the table is built at runtime from `init.data`
(0x344 bytes plus 0x5d8 of relocations), which is where to look next.

## Why this blocks the objective

`viewUnified2.so` never gets dlopened because the UI blocks in this CP
handshake, so the hooks never receive `views_base`, so `ne_add_row` is never
reached. Everything on the AP side is proven: the library loads into im.elf via
`/etc/ld.so.preload`, the dlopen override fires, libObj's base resolves.

Closing this needs either the missing host API surfaces implemented in the
emulator, or the upstream kernel tree rebuilt so the `-smp 1` comparison can be
made. Both are substantial; neither is a one-line change.

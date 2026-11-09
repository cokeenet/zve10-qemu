# First real-hardware result: the library loads for busybox, not for im.elf

## What was actually done on the camera

Deployed and rebooted (with owner approval):

```
/etc/libnewmenu.so      135,332 B  md5 7f924211…  (later 40cc0e2f…)
/etc/ld.so.preload      "/etc/zve10_agent.so" + "/etc/libnewmenu.so"
/etc/ld.so.preload.orig "/etc/zve10_agent.so"     (rollback)
```

Immediately after writing the file, before any reboot, every `busybox` process
began emitting our library's output:

```
NM === boot pid=1464 exe=/bin/busybox ===
NM LD_PRELOAD=<unset>
NM /etc/ld.so.preload readable=1
```

Hundreds of them. The environment variable was unset throughout, so the file was
the only loader input. `/log/newmenu.log` grew to 26 KB in a couple of minutes.

**That is the first real-hardware confirmation that `/etc/ld.so.preload` works on
this camera**, and it also answered the old open question in `BOOT-CHAIN-FACTS.md`
in the opposite direction from what that document concluded.

## Then a real problem, found because of the noise

`nm_log_budget` is a *per-process* static initialised to 600, and the boot
diagnostics were written **before** the `nm_is_ui_process()` guard. Every
fork+exec therefore wrote three lines, and the camera forks constantly. `/log` is
its own vfat partition (`/dev/nflasha11`), so this would have filled it.

Fixed by moving the guard ahead of every log call, so only `im.elf` logs at all.
After redeploy, `/log/newmenu.log` stayed at 0 bytes across twenty seconds of idle
— which is the correct behaviour *if* `im.elf` is not loading the library, and
tells us nothing on its own.

## The failure

After rebooting (twice, the second time by pulling the battery):

```
im.elf pid=157
libnewmenu map hits: 0
/log/newmenu.log:    0 bytes
```

The library does not reach `im.elf`.

## What this rules out, by static analysis

`/sbin/init` turned out to have a full symbol table, so its `main` could be read
directly. Sixteen `mount_partition` calls, in order:

```
0x9A1C  /proc
0x9AF6  /usr  ext2   (dnflasha15)      \
0x9B08  /usr  ext2   (nflasha15)        |  tried in turn
0x9B1A  /usr  cramfs                    |
0x9B2C  /usr  vfat                     /
0x9B32  nflasha10 -> /tmp   vfat
0x9B38  nflasha10 -> /etc   vfat     <- /etc
0x9B3E  /system
0x9B56  /setting
0x9B68  /log
0x9B6E  /lens
0x9B74  /cert
0x9B78  check_oreore2()
0x9B82  set_oreore_env()
0x9C84  fork()
0x9CCE  execve(bootin)
```

`/etc` is mounted at `0x9B38`; `bootin` is exec'd at `0x9CCE`, 0x194 bytes of
code later. So "the preload file was not there yet" is false, and `/tmp` and
`/etc` are two mount points onto the **same** partition.

`bootin` is stripped, but its literal pool at `0x9B60`–`0x9BB4` spells out what
it does, in order:

```
" target=qemu "  "Bb cmdline"
"LD_PRELOAD="  "/setting/mode/preload"
"gdb --args "  "/usr/bin/im.elf "  "debug "  "cho "  "cipa "
"dummy_susres "  "lazy-global "  "& "
"Bb enable Ctrl-C"  "Bb system()"  "Bb clearing env"
"LD_PRELOAD"  "MALLOC_OPTIONS"
```

with the code around `0x9A3C`–`0x9B3C` building a command line, calling
`system(...)`, then `unsetenv("LD_PRELOAD")` and `unsetenv("MALLOC_OPTIONS")`.

So `im.elf` is launched as `system("/usr/bin/im.elf debug cho cipa … lazy-global &")`,
i.e. **init → bootin → /bin/sh → im.elf**, not a direct `execve`. `init` also
carries a bare `LD_PRELOAD=` string, and `bootin` explicitly unsets the variable.

**`/setting/mode/preload` is the camera's own supported mechanism** for getting a
library into `im.elf`: `bootin` reads that file and forms `LD_PRELOAD=<contents>`,
which it then hands to the shell command line.

## The shell nesting was tested and is not the cause

Reproduced the three-level launch in the guest with `LD_PRELOAD` unset and only
`/etc/ld.so.preload` in play:

```
直接 exec:      maps 命中 4
sh -c 再套一层: maps 命中 4
LD_PRELOAD=[<unset>]
```

Both levels load the library. Indirect launch via `system()` is not what breaks it.

## So the honest state

| question | answer | how |
|---|---|---|
| does `/etc/ld.so.preload` work on this camera at all | **yes, proven** | busybox processes loaded it |
| does it reach `im.elf` | **no** | 0 map hits, twice, one battery-pull |
| is it a mount-order problem | no | `init` mounts `/etc` well before exec'ing `bootin` |
| is it the `system()`/shell indirection | no | measured in guest: still loads |
| was "no log" evidence of "no load" | **no** | `nm_log` silently skips when its path is unwritable |

## Next candidates, in order

1. **`/setting/mode/preload`** — the camera's own mechanism, which `bootin`
   deliberately consults. Population instructions are in the `bootin` literal
   pool above. This is the strongest lead because it is the path the vendor
   designed for exactly this purpose.
2. **Log visibility** — the new `nm_log.h` falls back `/log` → `/tmp` → `/etc`
   and reports which it used via `NM log=<path>`. Until a run prints
   `NM === boot pid=… exe=/usr/bin/im.elf ===` we do not actually know whether
   `im.elf` ran our constructor at all; absence of a log is not absence of load.
3. **`im.elf`'s own loading** — if (1) and (2) both come up empty, the next step
   is to see whether `im.elf`'s maps contain *any* preloaded library under the
   vendor's own mechanism, which would separate "loader ignores the file for this
   binary" from "our library specifically is rejected".

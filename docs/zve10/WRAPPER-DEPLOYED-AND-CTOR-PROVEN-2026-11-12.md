# The wrapper is deployed, and the library's constructor is proven on hardware

## The delivery problem, in one table

Every process on the camera, sorted by starttime, with how many mappings of each
preload library it has:

```
starttime  pid        agent  libnewmenu  comm
        0  1            0      0          launch_shell.el
       97  149          0      0          bootin.elf
      103  154          0      0          ash
      104  157          0      0          im.elf
      ...                          (all zero)
     2148  683          4      4          startBTCore.sh
     2150  684          4      4          bsa_server
     2451  749          4      4          startBTCore.sh
     2845  779          4      4          wpa_supplicant
     3475  787          4      4          pump
```

There is a hard boundary at starttime ~2148. Everything before it is a restored
snapshot image; everything after it was genuinely exec'd by the original init
scripts and therefore read the current `/etc/ld.so.preload`.

`ssboot` is `snapshot` and `image_mode` is 2, so this boot resumed rather than
cold-started. `im.elf` at starttime 104 was restored, not exec'd, so its loader
never ran. That is why writing the preload file has never helped: on a resume
boot nothing re-execs `im.elf`.

## Why it is not a library, path, or loader problem

Ruled out by measurement, not by argument:

- **The loader resolves it.** `/lib/ld-2.11.2.so --list /usr/lib/libnewmenu_usr.so`
  returns `rc=0` and shows all four dependencies found, and it even lists the
  preloaded agent library — proof the preload list itself is honoured.
- **No secure execution.** `AT_SECURE` is 0 in pid 1, 149, 157, 684, 779, 787.
- **The loader would complain if it skipped us.** A deliberately broken entry
  produces the message:
  `ERROR: ld.so: object '/usr/lib/DOES_NOT_EXIST.so' from LD_PRELOAD cannot be
  preloaded: ignored.`
  `im.elf`'s stderr is `/dev/console`, so such a message would be swallowed by
  the console device and be invisible over the network.

## The constructor works on this hardware

Rather than guess, the library was loaded into a process whose `cmdline` contains
`im.elf` — busybox copied to that name, so the library takes its UI branch but
the process cannot disturb the camera:

```
LD_PRELOAD=/usr/lib/libnewmenu_usr.so /log/nm/im.elf echo CTOR_TEST_RAN
```

`/log/newmenu.log` then contains:

```
NM === boot pid=3631 exe=/log/nm/im.elf ===
NM LD_PRELOAD=/usr/lib/libnewmenu_usr.so
NM /etc/ld.so.preload readable=1
NM ui pid=3631 lock=1
NM waiting for dlopen (no timer, hook-driven)
```

So on real hardware the library loads, its constructor runs, it identifies itself
as the UI process, takes the singleton lock, and waits for `libObj.so` to be
dlopen'd. Nothing about the library is broken.

## What was deployed

`/usr` turned out to be remountable read-write and writable, so the change is a
real file replacement rather than a bind mount that would vanish on reboot:

```
/usr/bin/im.elf.stock    the untouched original (md5 f1c746e3f52559658cd51354b210041e)
/log/nm/im.elf.stock     same, on vfat
/tmp/sd/nm/im.elf.stock  same, on the SD card (survives anything)
/usr/bin/im.elf.real     the original binary at the path the wrapper execs
/usr/bin/im.elf          the wrapper (8856 B, md5 74b4b6e16fc70d284038dff21ba4a840)
/usr/lib/libnewmenu_usr.so  the library (135332 B)
```

The wrapper is a plain dynamically linked ARM binary whose interpreter is the
camera's own `//lib/ld-linux.so.3`; its dependency list is just `libc.so.6`, and
the camera's glibc 2.11.2 exports every symbol it imports. A twin binary that
execs busybox instead of `im.elf` was run first to prove the whole path works on
this hardware:

```
=== tw pid=3550 uid=0 argc=1 ===
lib_readable=1
set LD_PRELOAD, exec /bin/busybox ...
CHILD_PID=3550
4                     <- libnewmenu mapped in the child
```

That is four mappings of the library in a child process, produced by a
locally-built ARM binary through the camera's loader. The mechanism works.

## What still has to happen

`bootin` forks and execs `im.elf` exactly once and then waits, so the wrapper
cannot take effect until `im.elf` is exec'd again. On this camera that means a
boot in which `im.elf` is genuinely re-executed rather than restored — which is
exactly the condition the earlier WBI work was trying to create, and which the
earlier attempts never achieved on this 2.03 firmware.

Two ways forward, and neither needs the WBI touched:

1. **Let the original init re-exec it.** The update flow restarts the application
   after a firmware write; a normal application restart through the camera's own
   UI is the same code path.
2. **Reach `im.elf` through a real cold boot**, which the official updater's
   updater-mode path can trigger without invalidating the WBI first.

On the first boot where the wrapper runs, `/log/nm_wrapper.log` will contain one
line confirming it ran and one confirming the exec, and `/log/newmenu.log` will
show `NM === boot` with `exe=/usr/bin/im.elf.real`.

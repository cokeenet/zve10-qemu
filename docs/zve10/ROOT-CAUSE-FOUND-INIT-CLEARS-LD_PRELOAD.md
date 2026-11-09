# Root cause: init clears `LD_PRELOAD` and replaces itself with launch_shell.elf

## The observation that cracked it

```
$ cat /proc/1/comm
launch_shell.el
$ cat /proc/1/cmdline | base64 -d
/usr/bin/launch_shell.elf\0 0 \0 1 \0
$ cat /proc/1/wchan
do_wait
```

**PID 1 on the camera is not `init`.** It is `launch_shell.elf`. The real `init`
exec'd over itself.

## The code path

At the tail of `main`, after the `bootin` launch section:

```asm
9f5c  ldr r3, =flag
9f5e  ldr r3, [r3]
9f60  cbnz r3, 9f68
9f62  ldr r0, =<partition>
9f64  bl  mount_partition

9f68  ldr r0, =0xA024          ; "LD_PRELOAD="      <-- EMPTY value
9f6a  blx putenv               ; ★ overwrites whatever was there

9f6e  bl  profile_exit
9f72  bl  launch_shell         ; ★ execl("/usr/bin/launch_shell.elf", ...)
9f76  ldr r3, =stderr
9f78  ldr r1, ="init %s:(%d): launch_shell() failed...\n"
9f82  blx fprintf
9f86  ldr r0, ="/bin/ash --login"
9f88  bl  im_oreore_system
9f8c  b   9f86                 ; infinite retry loop
```

and `launch_shell` itself, at `0x9994`:

```c
snprintf(a, 2, "%s", flag1 ? "1" : "0");
snprintf(b, 2, "%s", flag2 ? "1" : "0");
signal(SIGCHLD, SIG_IGN);
execl("/usr/bin/launch_shell.elf", "/usr/bin/launch_shell.elf", a, b, NULL);
```

## Why `env.txt` never worked

The order inside `main` is:

1. `0x9B82` — `set_oreore_env()` runs `putenv("LD_PRELOAD=/etc/nmlib.so")`,
   correctly parsed from `/setting/env.txt`. **The variable is set here.**
2. `0x9CCC` — `execve(bootin, argv, __environ)` is attempted on one branch.
3. `0x9F6A` — `putenv("LD_PRELOAD=")` **overwrites the same variable with an empty
   string.**
4. `0x9F72` — `launch_shell()` execs over the process.

So step 1 does its job and step 3 undoes it, by name, with an empty value. This is
not a parser bug, not an mmap failure, not `bootin`'s `unsetenv`, and not a
mount-order problem — it is `init` deliberately blanking the variable on the path
it actually takes.

## Proof from the camera's own memory

```
/proc/1/environ   (183 B)          /proc/157/environ  (171 B)
  HOME=/                             HOME=/
  TERM=linux                         TERM=linux
  PATH=...                           PATH=...
  TOPRC=/etc/toprc                   TOPRC=/etc/toprc
  MALLOC_OPTIONS=gfffffffff          MALLOC_OPTIONS=gfffffffff
  MALLOC_MMAP_THRESHOLD_=32768       MALLOC_MMAP_THRESHOLD_=32768
  MALLOC_TRIM_THRESHOLD_=32768       MALLOC_TRIM_THRESHOLD_=32768
  LT_ABORTREASON=0                   LT_ABORTREASON=0
  LD_PRELOAD=          <-- empty     (absent)
```

`PID 1` carries `LD_PRELOAD=` with an empty value, exactly the 12 bytes
(`LD_PRELOAD=\0`) that `0x9F6A` writes. `im.elf` does not carry it at all, because
`bootin` or the shell between them dropped the empty entry.

The previous blocker-A description said `init`'s own seven `putenv` variables were
present and only the `env.txt` one was missing. That reading was **backwards**: the
variable that is present is not the one `env.txt` asked for, it is the blank one
that replaced it.

## What this means for the `/etc/ld.so.preload` route

Nothing here touches `/etc/ld.so.preload`. It is not an environment variable,
`putenv("LD_PRELOAD=")` cannot blank it, and `unsetenv` cannot remove it. It was
already measured to work on this camera for every `busybox` process.

But it also explains why `im.elf` shows zero map hits even though the file is in
place and readable: on this boot path, PID 1 is `launch_shell.elf`, and the
process chain that finally starts `im.elf` is
`launch_shell.elf -> ... -> bootin -> sh -> im.elf`. Whatever `/etc` the loader
sees at the moment `im.elf` is exec'd is the `/etc` of that chain, and that is now
the thing to check rather than the environment.

## Two consequences worth stating

1. **`env.txt` is definitively closed.** Even with a perfect parser, `init` blanks
   the variable 0x2A8 bytes later on the path it takes. Any future attempt to use
   that file for `LD_PRELOAD` on this firmware will fail the same way.
2. **The camera is running a fallback boot path.** PID 1 being
   `launch_shell.elf` rather than `init` is not the normal end state; `main` only
   reaches `0x9F72` after the primary launch section. That is worth understanding
   on its own, independently of the menu work, because it may mean the normal
   `bootin` handoff is failing and the debug shell is covering for it.

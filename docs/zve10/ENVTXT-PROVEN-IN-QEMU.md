# env.txt reaches im.elf: proven in QEMU

## Method

Replaced `/usr/bin/im.elf` with a small program that dumps its own `argv`,
glibc's `environ`, and the kernel's `/proc/self/environ` copy. Left the rest of
the boot alone, used the upstream 14-module init, and put two lines in
`/setting/env.txt`:

```
LD_PRELOAD=/etc/nmlib.so
TESTVAR_FROM_ENVTXT=hello
```

The init script reads that file the way `set_oreore_env` does and exports each
line. The hardcoded `LD_PRELOAD=/usr/lib/libnativeentry.so` that ships in the
upstream script was removed first, so the file is the only source.

## Result

```
ENVTEST: reading /setting/env.txt
ENVTEST: export LD_PRELOAD=/etc/nmlib.so
ENVTEST: export TESTVAR_FROM_ENVTXT=hello
ENVTEST: LD_PRELOAD=[/etc/nmlib.so]

ERROR: ld.so: object '/etc/nmlib.so' from LD_PRELOAD cannot be preloaded: ignored.
   (27 times -- expected, /etc/nmlib.so does not exist in this initramfs)

=== FAKE im.elf ===
argc=11
  argv[0] = /usr/bin/im.elf
  argv[1] = debug
  argv[2] = target=qemu
  argv[3..9] = s g r u l i a
  argv[10] = lazy-global

--- environ ---
  LD_LIBRARY_PATH=/usr/lib:/lib
  HOME=/
  TERM=linux
  PATH=/bin:/sbin:/usr/bin:/usr/sbin
  LD_PRELOAD=/etc/nmlib.so          <- arrived
  PWD=/
  TESTVAR_FROM_ENVTXT=hello         <- arrived

--- /proc/self/environ (kernel copy) ---
  (identical)
```

The `ld.so: cannot be preloaded` line is the proof rather than a problem: the
loader was told to open the path, which only happens when `LD_PRELOAD`
reached it.

## What this settles

- init's environment is inherited by bootin and passed to im.elf intact. A
  variable set from `/setting/env.txt` shows up in the child, and so does an
  arbitrary one.
- `TERM=linux` is not an anomaly. It appears in this run too, because the
  init is a shell script run by busybox ash, which sets it. On hardware the
  same variable can come from a busybox-built environment.
- The kernel copy at `/proc/self/environ` equals glibc's `environ`, so reading
  `/proc/PID/environ` on hardware does reflect the envp that was passed.

## What changed in the picture

The previous round concluded the hardware envp looked "assembled" because
`TERM=linux` sat between `HOME=/` and `PATH=...`, and inferred that bootin does
not forward `*environ`. This run shows a normal forward produces exactly that
shape -- `HOME`, `TERM`, `PATH`, then the appended variables. So that inference
was wrong, and bootin forwarding `*environ` stands.

## So the hardware failure is not the chain

Everything between `/setting/env.txt` and im.elf's environment is demonstrated
to work. On hardware the library still did not appear. The difference has to be
in what hardware provides, not in how the variable travels. Candidates left:

1. On hardware `/setting/env.txt` was not yet readable when init got there, or
   the vfat mount had not settled. Our probe test read a vfat image successfully,
   but that was a loop mount made by the same kernel, not nflasha2.
2. The library path named in `env.txt` did not exist at the moment ld.so
   looked. `/etc/nmlib.so` was verified present and md5-correct after upload,
   but the upload happened in the same boot that was then restarted -- worth
   re-checking whether `/etc` is repopulated from somewhere on boot.
3. Something later in the hardware boot replaces im.elf's environment or
   restarts im.elf without the variable.

For (2) there is a concrete suspicion worth testing: `/usr` is force-restored
on boot on this camera, and `/etc` may also be refreshed from a vendor copy.
If `/etc/nmlib.so` disappeared between the upload and the reboot, ld.so would
log exactly the "cannot be preloaded" error that QEMU shows, and im.elf would
come up without the library -- which matches every hardware observation.

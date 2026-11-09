# Route B stopped reaching the settings page; what the diff shows

## Symptom

After several rounds of rebuilding the kernel and repopulating the initramfs,
route B no longer gets past CP init:

```
gate1..gate3, rc1..rc4:  dlopen count 21,  NE_TREE_ADDED 0
back4 (earlier, good):   dlopen count 125, NE_TREE_ADDED 1
```

Four consecutive runs, all identical, so it is systematic rather than the
intermittent CP timing seen before.

## Configuration is the same

```
init:     LD_PRELOAD=/usr/lib/libm2probe.so:/usr/lib/libuiobserve.so:/usr/lib/libnewmenu.so
modules:  14 loaded, all status=0
mknod:    uipc backup dmpgles2 stream stream2 ldec
av-cam.bin: cdcae9d4fdbf66a33704a4c7564e346d  (the camera build, correct)
im.elf:      f1c746e3f5255965                  (identical to ref_e10)
bootin.elf:  d8bfb4f7fc9b71ee                  (identical)
```

The CP init sequence is identical up to the point it stops:

```
M2INIT pid=... cmd=/usr/bin/bootin.elf
M2SND qid=008f013a lib=/usr/bin/bootin.elf off=00001114
M2SND qid=003e014d lib=/usr/lib/libbackup.so off=00001010
M2INIT pid=... cmd=/usr/bin/im.elf
M2SND qid=008f013a lib=/usr/bin/im.elf off=0000311e
M2SND qid=003e014d lib=/usr/lib/libbackup.so off=00001010
M2SND qid=0094016a lib=/usr/bin/im.elf off=00001a3e
M2SND qid=00924127 lib=/usr/lib/libInfraAccessLog.so off=00001a52
```

Both then print `entry_main`, and only the good run continues.

## What actually differs

| signal | back4 | now |
|---|---|---|
| `dlopen` calls | 125 | 21 |
| `UI_OBSERVE` lines | 7,965 | 209 |
| `KEYINJECT` lines | 16 | 0 |
| `dlopen path=libObj.so` (relative) | present, handle `0x34078` | absent |
| `dlopen path=/usr/lib/libObj.so` | 5+ times | once |

`KEYINJECT` is emitted by `libuiobserve.so`. Zero occurrences means that library
loaded but never completed its registration, and the missing relative-path
`libObj.so` load points the same way: in the good run something dlopened
`libObj.so` by name and got a different handle (`0x34078`) than the absolute-path
load, i.e. two distinct views of the library. That no longer happens.

## What is not the cause

`libnewmenu.so` was rebuilt twice in this window but it:

- does not rewrite the path it forwards to the real `dlopen`
- is loaded third in `LD_PRELOAD`, after m2probe and uiobserve
- only hooks `dlopen` to observe, and the observed paths above show it passing
  them through unchanged

The gate widening changed only `ne_init_wrapper`'s acceptance test, which cannot
affect whether `initSettingNode` is ever called.

Two things did change outside the library: the kernel was rebuilt several times
(current md5 `846e58d2`, back4's was `cc624d83`), and `usr/lib` in the initramfs
was repopulated after I cleared it. `libm2probe.so`'s whole purpose is to paper
over a CP handshake whose reply never arrives, and that shim is sensitive to
exact timing and memory layout, so a different kernel build is a plausible cause
and a repopulated `usr/lib` changes what `LD_LIBRARY_PATH` finds first.

## Where this leaves things

The verified result is not lost. It stands on route A with the same libraries and
the same patched QEMU:

```
NE_MODULES {hooks:4}  NE_VERSION {last:6}
NE_TREE_ADDED {5 -> 6}
NE_FOCUS_ENTRY {group:5,page:5,index:6}
NE_ROW x5  NE_READY {title_sets:5,native_glyph_images:7}
UI_CAPTURE_META {segments:151, fingerprint 38ededc15a35173a}
```

with three frames whose hashes equal upstream's published captures, produced both
by upstream's prebuilt library and by one compiled from this repository's own
sources.

Route B's value was corroboration with the camera's own `viewUnified2.so`; it is
not the acceptance path, and nothing shipped depends on it. Restoring it means
reproducing the earlier kernel build exactly -- `cc624d83` is the one that worked
-- or bisecting the initramfs contents against the state before the cleanup.

## Follow-up (round 31)

Checked the remaining candidates:

| candidate | finding |
|---|---|
| `libuiobserve.so` version | two builds exist, `b1027b58` (78,944 B) and `7d387565` (78,948 B). Swapped in the second: identical failure, 21 dlopens. Reverted. |
| `libm2probe.so` version | `276126d1` in use; `zv_m2probe` has a different one (`653b899e`). |
| init script | diff against upstream is one line: the `LD_PRELOAD` list. |
| module list | all 14 identical to the good run, all `status=0`. |
| device nodes | same six `mknod` entries. |
| `av-cam.bin` | `cdcae9d4`, the camera build, unchanged. |
| `im.elf`, `bootin.elf` | byte-identical to `ref_e10`. |
| `usr/lib` contents | only three files added by me; the other 144 are original. |

One thing did surface: the kernel banner.

```
back4 (good):  #110  Mon Nov 9 06:28:19 CST 2026
now:           #518  Mon Nov 9 12:18:28 CST 2026
```

408 rebuilds separate the two, each of which re-embeds the initramfs and bumps
the build stamp. The source is unchanged, so this is not a code difference --
but `libm2probe.so` exists precisely to patch over a CP handshake whose reply
never arrives, and that kind of shim is sensitive to timing and memory layout
in ways a `#`-number cannot capture.

So the honest state is: route B's good run depended on a kernel build that no
longer exists on this machine, and I cannot currently reproduce it by swapping
libraries or scripts.

## Recovery options, in order of cost

1. Rebuild until the CP handshake completes again. `#110`'s exact bytes are gone
   but the source is stable, so repeated builds may land on a working layout --
   the failure is deterministic per build, not per run, which makes this cheap to
   test: build once, run once.
2. Bisect `usr/lib` and `usr/kmod` against the state before the cleanup. I have
   no snapshot of that state, so this would be reconstruction by trial.
3. Give up route B. It corroborates against the camera's own `viewUnified2.so`
   but is not the acceptance path, and nothing shipped depends on it.

Option 1 is worth a few attempts because it is a single build-and-run per try.
Option 3 is what will happen if that fails, and it costs nothing: the verified
result is complete on route A.

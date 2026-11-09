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

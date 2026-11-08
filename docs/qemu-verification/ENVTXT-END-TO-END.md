# /setting/env.txt end to end, in QEMU

## What was tested

Not the library (that was already proven), but the **delivery path**: does a
plain text file on /setting actually reach im.elf as LD_PRELOAD.

The QEMU rootfs init is a shell script, so it was modified to do what the real
init's `set_oreore_env` does:

```sh
if [ -f /setting/env.txt ]; then
    while IFS= read -r __line; do
        case "$__line" in ''|'#'*) continue ;; esac
        export "$__line"
    done < /setting/env.txt
fi
```

The hardcoded `LD_PRELOAD=/usr/lib/libnewmenu.so:...` was removed from the
bootin line, so the only source of LD_PRELOAD is the file.

`/setting/env.txt` contained one line: `LD_PRELOAD=/z.so`

## Result

```
ENVTEST: found /setting/env.txt
ENVTEST: applying LD_PRELOAD=/z.so
ENVTEST: LD_PRELOAD=/z.so
```

and then every process on the boot path reports it:

```
NM === boot pid=59  exe=?                     NM LD_PRELOAD=/z.so
NM === boot pid=60  exe=/bin/busybox          NM LD_PRELOAD=/z.so
...
NM === boot pid=113 exe=/usr/bin/ulogio.elf   NM LD_PRELOAD=/z.so
NM === boot pid=117 exe=/usr/bin/bootin.elf   NM LD_PRELOAD=/z.so
NM === boot pid=120 exe=/usr/bin/sen.elf      NM LD_PRELOAD=/z.so
NM === boot pid=127 exe=/usr/bin/im.elf       NM LD_PRELOAD=/z.so
```

31 processes in total, including the UI process itself.

`libObj.so` was matched 21 times, with `object_base=0x9adc000` resolved each
time, so the library was not merely present but working.

## Why this matters

Earlier QEMU runs had LD_PRELOAD written directly into the init script, which
proved the library works but said nothing about /setting/env.txt. This run
removes that shortcut: the file is the only source, and it still reaches im.elf.

Combined with the disassembly facts in `docs/zve10/BOOT-CHAIN-FACTS.md` and
`tools/zve10/envfile/README.md`, the delivery path now has both static and
dynamic evidence.

## What is still only testable on hardware

The real init's `check_mode_file` gate, the vfat mount of /setting at
`0x9B56`, and img's actual UI. QEMU's shell init does not exercise the gate.

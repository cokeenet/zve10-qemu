# What the 22 reference frames actually prove

Criterion 2 of the objective cites
`/mnt/build/zips/2026-11-07-zve10-native-menu-entry/output/native-window-v14/`
as "19 screenshots proving it is feasible". I opened them this round and
checked the metadata, because the distinction matters for how much credit the
reference deserves.

## The frames do show the target state

| file | content |
|---|---|
| `capture-005.png` | セットアップ5, five rows, cursor on プライバシー通知 (last original row) |
| `capture-006.png` | same page, cursor moved **down to New Menu** |
| `capture-010.png` | ネットワーク page, empty list |

`capture-005` and `capture-006` differ; `capture-006` and `capture-007` are
byte-identical. That is the signature of a real list: moving the cursor produces
a new frame, and the resting frame is stable. A painted overlay would not behave
that way across two captures.

So the visual target — "New Menu" as a sixth, selectable row after the five
original rows of セットアップ5 — is confirmed by these frames.

## But they were not produced by Sony's bootin/im on the camera

`build.json` says:

```
guest_programs_executed : False
original_files_modified : False
input_model             : original-native-function-event; physical panel mapping unverified
```

and the recorded build command compiles `observer-derived.c` plus `menu_shell.c`
into `libnativeentry.so`, linked with `-ldl -lpthread -lrt` on the **host**
toolchain path `/home/choco/Downloads/zve10-qemu/...`.

The `init` inside `native-ui.cpio` confirms the shape of the run:

```sh
( LD_PRELOAD=/usr/lib/libuiobserve.so /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal ... ) &
sleep 100000
```

So `bootin.elf` *is* started, but the presentation path is the observation
shim, and the frames are the shim's output surface — a host harness driving the
same native row/title functions, not a photograph of the camera's LCD.

## Consequences I need to keep straight

1. The reference proves the **rendering and layout** are correct for this
   platform: real font, real row metrics, real cursor, right Japanese titles,
   five original rows preserved, new row sixth.
2. It does **not** prove that `viewUnified2.so`'s live tree was edited, because
   `original_files_modified: False` and no Sony UI library was patched in that
   run.
3. Therefore the QEMU acceptance item is **partially** satisfied: the visual
   state is reproduced and evidenced, but through a harness rather than through
   the camera's own menu stack. The stronger claim — Sony's own menu building
   `New Menu` — is what our `libnewmenu.so` hooks (`ne_hook` on
   `CautionConfig+0xb8dffa`, `views_base+0x3014f0`, `+0x2fccc0`, `+0x862130`)
   are for, and that is still only proven in our own QEMU runs, not by these
   frames.
4. The reference library does not match ours: `libnativeentry.so` here hashes to
   `e4f4006b…` while `build.json` records `fc78edbe…`, so this directory is not
   byte-identical to the run it describes either. The PNGs are still the useful
   artifact; treat the hashes as stale.

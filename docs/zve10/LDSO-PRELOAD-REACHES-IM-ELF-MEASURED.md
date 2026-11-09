# `/etc/ld.so.preload` does reach the boot-time im.elf — measured

## The mystery this closes

`BOOT-CHAIN-FACTS.md` recorded an unsolved problem:

> A manually spawned `im.elf` on the running camera did load libraries from that
> file (four map hits each for `tiny.so` and `zve10_agent.so`), while the original
> `im.elf` did not. … The difference must lie in something specific to how the
> first `im.elf` is set up. **This remains open.**

It mattered because the whole delivery route rests on that file working for the
process that is started by `init → bootin → execve`, not for one started by hand.

## The measurement

Removed `LD_PRELOAD` from `init` entirely, so the only thing that could load our
library was `/etc/ld.so.preload`. Then, three seconds after the UI came up, the
guest printed the maps of whatever process was named `im.elf`:

```
MAPCHECK pid=121 comm=im.elf
MAPCHECK libnewmenu_hits=4
MAPCHECK preload_content=[/usr/lib/libnewmenu.so]
```

Four map entries for `libnewmenu.so` inside the boot-time `im.elf`. The
environment variable was absent the whole time:

```
NM LD_PRELOAD=<unset>
```

And the hooks installed normally:

```
NM caution anchor OK
NM hooked CautionConfig init
NM hooked viewUnified2 (3)
NM installed object=0x9b14000 views=0x32176000 modules=1
```

## So the earlier conclusion was wrong

The boot-time `im.elf` **does** honour `/etc/ld.so.preload`. What the old note
actually observed is most likely one of:

- the library it named was not present at that moment, in which case the loader
  prints `ignored` and the process runs unhooked — a silent no-op, easy to
  mistake for "the file was not read";
- or the maps were inspected for a different pid than the one holding the loaded
  copy.

Either way, the "specific to how the first im.elf is set up" hypothesis is not
needed and is now contradicted by direct observation.

## What this settles for the delivery route

The route is:

1. `/etc/libnewmenu.so` — the library
2. `/etc/ld.so.preload` — one line naming it
3. nothing else

No `LD_PRELOAD` in any environment, no `init` patch, no `/setting/env.txt`, no
dependence on `bootin`'s envp. And now measured on both halves:

| question | answer |
|---|---|
| does this loader read the file? | yes (`LDSO-PRELOAD-IS-NOT-FATAL-MEASURED.md`) |
| does a failed entry break exec? | no — it prints `ignored` and continues |
| does the boot-time `im.elf` load from it? | **yes — 4 maps, `LD_PRELOAD` unset** |

## Why `NE_TREE_ADDED` was still zero in this run

Not a delivery failure. In this configuration `libuiobserve.so` was absent, and
that library is what keeps the QEMU UI stepping far enough to build the Settings
page — the same effect documented in
`QEMU-VS-HARDWARE-THE-UIOBSERVE-DIFFERENCE.md`. On a camera the UI is driven by
real hardware and that page is built when the user opens the menu.

So this experiment answers the delivery question in isolation, which is exactly
what it was built to do.

## Reproduce

The guest ran this after `UI_AFTER_SLEEP_3`:

```sh
for p in /proc/[0-9]*; do
    c=$(cat $p/comm 2>/dev/null)
    if [ "$c" = "im.elf" ]; then
        echo "MAPCHECK pid=${p#/proc/} comm=$c"
        echo "MAPCHECK libnewmenu_hits=$(grep -c libnewmenu $p/maps 2>/dev/null)"
    fi
done
echo "MAPCHECK preload_content=[$(cat /etc/ld.so.preload 2>/dev/null)]"
```

with `init` having no `LD_PRELOAD` assignment at all.

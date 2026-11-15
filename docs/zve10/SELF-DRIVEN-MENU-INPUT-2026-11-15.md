# Driving the menu from the agent, so no physical key press is needed

## The problem with waiting for a human

`NE_LATE_INSERT` lives on `ne_populate_wrapper` and `ne_main_wrapper`, which only
run while the settings menu is on screen. After the camera rebooted (new
`boot_id fc62fac9…`, uptime 162 s) I re-injected and the log showed the hooks
installed again, but `grep -c NE_ /log/newmenu.log` was still 0 — because nobody
had opened the menu.

Asking for a key press every iteration is a bad loop. The library already knows
how to synthesise the camera's *own* menu function events; it was only wired to
the QEMU serial protocol.

## Confirmed: the camera really does restore im.elf from a snapshot

```
boot_id      fc62fac9-b6e5-4cf6-9d36-5ec18b491e30   (changed — camera did reboot)
uptime       162 s
/proc/1/stat starttime        0      launch_shell — really started at boot
/proc/157/stat starttime    104      im.elf      — did NOT start at boot
```

A process that actually exec'd would have a starttime far higher than 0..162.
104 means it was restored from the warm-boot image. That is the final explanation
for every earlier delivery failure: its loader never ran, so `/etc/ld.so.preload`
and `/settin/env.txt` cannot reach it, and its menu tree predates our library, so
the `views_base + 0x301921` gate can never fire.

## The mechanism the library already had

`ne_post_function()` builds a real native menu event and pushes it into the
camera's own `EventManager`:

```c
uint32_t id = ((uint32_t (*)(uint32_t,uint32_t,uint32_t))(object_base + 0x117789))
                  (0x13000000u, 0xbau, function);
((void (*)(void *, uint32_t, unsigned, unsigned))(object_base + 0x8a01e9))(event, id, 4, 1);
((int  (*)(void *, void *, int))(object_base + 0x8a1795))((void *)events, event, 1);
```

This is the *same* channel the QEMU reference used, and its key mapping is
explicit in `transport.inc`:

```c
static const char *const names[] = {"DOWN","UP","RIGHT","LEFT","ENTER","MENU"};
for (function = 1; function <= 6; ++function)
    if (!strcmp(argument, names[function - 1])) break;
```

So function numbers are: 1 DOWN, 2 UP, 3 RIGHT, 4 LEFT, 5 ENTER, 6 MENU.

In QEMU the harness drives the whole acceptance sequence with exactly these
events — UP, DOWN, ENTER, UP, DOWN, MENU, ENTER, MENU — and asserts pixel
equality at each step. On hardware we can post the same events and get the same
menu behaviour, driven entirely by the agent.

## Two new exports

```c
__attribute__((visibility("default")))
int nm_press(unsigned function);          /* post one native menu function event */

__attribute__((visibility("default")))
int nm_status(unsigned *out);             /* object_base, views_base, modules,
                                             installed, late_attempts,
                                             title_sets, entries, exits */
```

and a generic ptrace caller so any exported symbol can be invoked in the target
process with up to two integer arguments:

```
ptcall.elf <libpath-substr> <local.so> <symbol> [a0] [a1]
```

It resolves the library base from `/proc/<pid>/maps`, takes the symbol offset
from the local `.dynsym`, plants `BKPT` at `0x9000`, calls, reads `r0` before
restoring anything, and reports `stop_is_ours`.

## Intended sequence, fully automatic

```
inject libnewmenu6.so
ptcall libnewmenu nm_status  -> confirm modules=1 installed=0
ptcall libnewmenu nm_press 6 -> MENU   (open settings)
ptcall libnewmenu nm_status  -> expect NE_TREE_ADDED, installed=1
ptcall libnewmenu nm_press 1 -> DOWN ...  (walk to セットアップ5)
ptcall libnewmenu nm_press 5 -> ENTER  (enter New Menu)
ptcall libnewmenu nm_press 6 -> MENU   (back to parent)
```

Then `/log/newmenu.log` carries `NE_TREE_ADDED`, `NE_FOCUS_ENTRY`,
`NE_INPUT_CONSUMED` for each step, which is the same evidence the QEMU
acceptance asserts on.

## Status

Code is built; the camera went unreachable (`No route to host`) before lib6
could be injected, so none of this has run on hardware yet.

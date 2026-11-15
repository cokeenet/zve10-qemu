# The ptrace injection works: the library is now inside the live im.elf

## Result

```
pid=157 ui_tid=157
libdl=0x0400E000 dlopen=0x0400EBB0
ui thread PC=0x0438B17C SP=0x5EC91C04 CPSR=0x00000010
strbuf=0x5EC89C04 ok
trap: PC=0x00009000 LR=0x040EAD3D CPSR=0x60000010 r0=0x0D06CC80
stop_is_ours=1
maps_after=4
VERDICT LIBRARY_MAPPED
```

`/proc/157/maps` gained four mappings for `libnewmenu`, and
`/proc/157` is still alive with 200 threads afterwards.

The library's constructor ran and wrote its own boot record to
`/log/newmenu.log` (file grew 78,438 → 78,864 bytes):

```
NM === boot pid=157 exe=/usr/bin/im.elf ===
NM LD_PRELOAD=<unset>
NM /etc/ld.so.preload readable=1
NM log=/log/newmenu.log
NM ui pid=157 lock=1
NM caution sym=0x17966b21 base=0x16dd8000 off=0xb8eb21 off_even=0xb8eb20
NM caution anchor OK
NM hooked CautionConfig init
NM hooked viewUnified2 (3)
NM hooked font titles
NM installed object=0x6265000 views=0x15f88000 modules=1
NM installed at ctor
```

So all four byte-verified hook sites are now installed **inside the running
camera UI process**: `CautionConfig` init, three `viewUnified2` sites, and the
font/title path. `modules=1` matches the required
`ne_modules_installed` state.

## Two bugs that had to be fixed, both mine

### 1. Wrong thread

`PTRACE_ATTACH` stops whichever thread the kernel picks. `im.elf` has 200 of
them, so the first attempt wrote registers onto a non-UI thread and the
observed `r0` was meaningless — proven by a control experiment where `dlopen`
of a path that cannot exist returned the *identical* value `0x040E2940`.

Fix: select the thread whose `comm` is `im.elf` (the main thread, `tid == pid`),
verify its `PC` lies inside an `x` mapping before writing anything.

### 2. The path string was being clobbered by dlopen's own stack frame

First successful-looking run returned `r0 = 0` and `dlerror` said:

```
/etc<garbage>db: cannot open shared object file: No such file or directory
```

The string had been written at `SP-0x200`. `dlopen` grows the stack downward
and its own frame overwrote it. Measured stack mapping is 132 KB
(`5ec71000-5ec92000`), so the buffer was moved to `SP-0x8000`, which is far
outside any frame `dlopen` will build and still well inside the stack.

## What is still missing

The hooks are installed but **no `NE_*` event has fired**: `/log/newmenu.log`
contains zero `NE_VERSION`, `NE_TREE_ADDED` or `NE_FOCUS_ENTRY` lines after the
install. That is expected and is the known open problem recorded earlier: the
insertion gate is

```c
static int ne_caller_may_insert(uintptr_t caller)
{
    return views_base && caller == views_base + 0x301921;
}
```

i.e. the row is only added when the original `CmnViewSettingNode` constructor
runs **after** our hooks are in place. `im.elf` is a snapshot-resumed process
that already built its menu tree long ago, so that call site will not run again
in this process lifetime.

So delivery is solved; **insertion timing is not**. The options are:

1. **Add a second insertion path** that does not depend on
   `ne_caller_may_insert` — e.g. trigger `ne_add_row` when the menu view is
   next entered (we already have a `ne_bind_after_draw` style hook available
   from the QEMU build).
2. **Force the original constructor to run again** by driving the settings
   menu through the same native path the camera uses on MENU press, so the
   gated call site fires.

Option 1 is the smaller change and does not depend on driving UI input.

## Files

`ptinject3.c` / `ptinject3.elf` (13,276 B) is the working injector.
`ptdlerr.c` / `ptdlerr.elf` reads the loader's error string out of the target
process, which is how bug 2 was found.

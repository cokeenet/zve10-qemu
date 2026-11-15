# Second insertion path, because the first can never fire on hardware

## Why the original gate is dead on the camera

`native_entry.inc` only adds the row here:

```c
static int ne_caller_may_insert(uintptr_t caller)
{
    return views_base && caller == views_base + 0x301921;
}

static void ne_init_wrapper(void *root, void *out, int flags)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (ne_modules_installed && !ne_installed && ne_caller_may_insert(caller))
        ne_add_row(root);
    ne_original_init(root, out, flags);
}
```

`caller == views_base + 0x301921` is the return site of the original
`CmnViewSettingNode` constructor. On hardware `im.elf` is **snapshot-resumed**
(boot id unchanged, `starttime` far below fresh processes): its menu tree was
built before our library existed, and that call site will not run again in this
process lifetime. So hooks install, and `ne_add_row` is never called —
`grep -c NE_ /log/newmenu.log` stayed 0 through every successful install.

## The replacement

Two entry points that *do* run after a late load:

| hook | site | when |
|---|---|---|
| `ne_populate_wrapper` | `views_base + 0x3014f0` | every settings page populate |
| `ne_main_wrapper` | `views_base + 0x2fccc0` | every menu main-loop iteration |

Both now call:

```c
static int ne_late_insert(void *view)
{
    if (!ne_modules_installed || ne_installed || ne_late_attempts >= NE_LATE_MAX) return 0;
    void *late_root = ne_late_root(view);   /* view+0x1b8, else view */
    if (!late_root) return 0;
    if (ne_late_root_seen || ne_pointer((uintptr_t)late_root) == 0) return 0;
    ++ne_late_attempts;
    ne_late_root_seen = 1;
    int status = ne_add_row(late_root);
    ne_line("NE_LATE_INSERT {\"attempt\":%d,\"root\":%u,\"status\":%d}\n", ...);
    return 1;
}
```

Bounded to 8 attempts, and it logs its own outcome, so a failure is visible
rather than silent.

## Verified on the camera

The four sites now point into the freshly injected library, byte-checked:

```
init      dff804f0 00bf | 64b1f913 e830   literal 0x13F9B164 = lib4 base + 0x2164
populate  dff800f0 | 4ca9f913 | 28010023  literal 0x13F9A94C = base + 0x194c
main      dff800f0 | e0adf913 | 16460d46  literal 0x13F9ADE0 = base + 0x1de0
row       dff800f0 | 0ca4f913 | 80468946  literal 0x13F9A40C = base + 0x140c
```

rebased onto `13f99000-13fb8000 r-xp /etc/libnewmenu4.so`, all four resolve
inside that library. Log confirms:

```
NM hooked CautionConfig init
NM hooked viewUnified2 (3)
NM installed object=0x6265000 views=0x15f88000 modules=1
```

## Two practical notes learned this round

- `dlopen` caches by path. Re-injecting `/etc/libnewmenu.so` after rebuilding
  returns the **old** handle and runs nothing. Each rebuild needs a new
  filename (`libnewmenu2.so`, `3`, `4`, ...).
- Installing a second copy over a library that is already hooked fails at
  `memcmp(anchor)` with `NM FATAL: caution init hook failed (anchor or patch
  mismatch)`. The four (six, with the font pair) sites must be restored to the
  pinned original bytes first — `sysread.elf <pid> <addr> 12 <hex>` does that,
  and readback confirms `MATCH`.

## Still required to see the row

`NE_LATE_INSERT` has not fired yet because **the settings menu has not been
opened since the install**. Both hooks live on the settings view, so the camera
must be driven to MENU → セットアップ5 before anything happens. That is a
physical-action step, not something the agent can trigger.

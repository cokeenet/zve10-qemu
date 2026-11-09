# Why `ne_add_row` fires on route B but not route A

## The gate

```c
static void ne_init_wrapper(void *root, void *out, int flags)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (ne_modules_installed && !ne_installed && caller == views_base + 0x301921) {
        int status = ne_add_row(root);
        ...
    }
    ne_original_init(root, out, flags);
}
```

## What that address is

`viewUnified2.so` at `0x301920`:

```
30190a: cbz  r0, 301920          ; if the node is null, skip the call
30190c: ldr  r3, [r0, #0]        ; vtable
30190e: ldr  r4, [r3, #8]        ; slot 2
301910: bl   2f68f8
301914: ldr  r3, [pc, #144]      ; global
301916: ldr  r3, [r6, r3]
301918: ldr  r2, [r3, #0]
30191a: mov  r1, r0
30191c: mov  r0, r9
30191e: blx  r4                  ; <- the call this gate watches
301920: mov  r0, r5              ; <- return address = base + 0x301920, Thumb bit set
```

So `views_base + 0x301921` is the address just after `blx r4`, and it is an
**indirect** call through vtable slot 2. The hooked function itself is
`CmnViewSettingNode::initSettingNode` at `CautionConfig.so + 0xb8dffa`:

```
b8dffa <_ZN18CmnViewSettingNode15initSettingNodeEPPS_i>:
  b8dffa: ldr  r3, [r0, #0]
  b8dffc: stmdb sp!, {r4,r5,r6,r7,r8,lr}
  ...
  b8e00c: blx  r3              ; vtable slot 58
  b8e01e: blx  r5              ; vtable slot 62
  b8e02a: blx  r3              ; vtable slot 59
```

## The asymmetry

| | route B | route A |
|---|---|---|
| `NM installed ... modules=1` | yes | yes |
| `NM hooked CautionConfig init` | yes | yes |
| `NM hooked viewUnified2 (3)` | yes | yes |
| font hooks (after the 12-byte fix) | 0 warnings | 0 warnings |
| `ne_add_row` fires | **yes** | **no** |

Everything up to the gate is identical. Only the indirect call arrives with a
different return address, which means the vtable slot that ends up reaching
`initSettingNode` is not the same slot in the two environments -- the two
`native-ui.cpio` / `kbuild-ui2` trees build the settings page out of different
call sites.

## Why this is not a defect in either library

The gate exists to identify one specific `initSettingNode` invocation: the one
that builds the page we want to extend. Hard-coding a single return address is
what makes it environment-specific. Upstream could do it because they ran one
fixed pair of kernel and cpio; this project has run four.

## What it means for the deliverable

The camera is the environment that matters, and on the camera this gate is the
one it was derived from -- `viewUnified2.so` there is `edb07179` (identical to
both local copies) and `CautionConfig.so` resolves `Ctor` at exactly
`0xb8eb21`, the offset the gate insists on. Route B reproduced the row with that
gate; route A does not because its settings page is assembled through a
different indirect slot.

If the camera ever turns out to build the page the way route A does, the fix is
to widen the gate: accept the return address of *any* call whose target resolves
to `ne_caution + 0xb8dffa`, rather than one hard-coded `views_base` offset. That
is a small change and is written down here so it is not rediscovered later.

## Reproduce the comparison

```
route B  tools/zve10/qemu/run.sh <name> 130        (kbuild-ui2 + our cpio)
route A  tools/zve10/qemu/run_routeA.sh <name> 500 (kbuild-ui + native-ui.cpio)
```

Route B's log shows `NE_TREE_ADDED {"old_count":5,"new_count":6}`; route A's
shows the same four hook lines and then nothing from `ne_add_row`.

# Settings-menu lifecycle: static facts and remaining reconstruction question

## Scope

Read-only local ELF inspection with the installed ARM GNU objdump and pyelftools.
No camera calls, UI actions, hooks, memory writes or reboot in this investigation.
The hardware owner already authorized integration, but that does not make an
unqualified lifecycle call correct. Current hardware acceptance is not achieved.

Artifact: /root/fw/ref_e10/usr/lib/viewUnified2.so, MD5
edb07179859e2fb66f59df4b7023cbbb (matches the earlier hardware hash).

## Factory creates a new object when called

Dynamic symbol ViewSettingMenuToInstance = 0x2f36f5, size 36, Thumb.
Disassembly at 0x2f36f4:

```
0x2f36f6  allocation size = 1192 (0x4a8)
0x2f36fe  operator new
0x2f3706  call object constructor at 0x2f2cfc
0x2f370a  return allocated object
```

This factory does not return an internally cached singleton. This does NOT prove
that the view manager calls the factory on every MENU reopen; manager caching is
a separate question.

The constructor initializes the ViewBaseForMR base, installs primary/secondary
vptrs, constructs members and clears model-request state. It does not directly
call the tree-init site in this constructor body.

## Native initialization entry and verified call site

The known menu vptr is +0xbaabb0. File-backed pointer slots:

```
virtual slot address 0xbaac94 (vptr + 0xe4):  0x301829
virtual slot address 0xbaacd0 (vptr + 0x120): 0x2fccc1
```

These are table ENTRY addresses, not table bases. The 0x301828 method initializes
many object fields, obtains a tree from +0x2f6a54, stores it at this+0x1b8,
then calls vtable slot 2 of the returned tree at 0x30191e (BLX r4).
The Thumb return address is +0x301921, exactly the upstream insertion guard.
The init hook inserts before the original initSettingNode executes.

The method at 0x3014f0 (currently named populate by our hooks) is distinct and
includes backup/model reads. Its invocation alone does not prove construction of
a new tree.

## Remove unproved generalized caller matching

The widened slim-only gate read instructions starting at raw caller-4, without
normalizing the Thumb return bit, and aligned PC as if all BL targets used BLX
semantics. It also cannot resolve the actual indirect BLX/vtable call above.
Even an accurate direct BL decoder would not establish that another caller owns
the intended settings tree.

The local slim source now accepts only the original verified
views_base+0x301921. It no longer dereferences arbitrary caller memory in the
fallback. test_caller_gate.py verifies exact matching, adjacent addresses,
missing base and arbitrary pointers. Default callback regressions and ARM object
compilation pass. No deployed camera library was replaced.

The !ne_installed guard prevents repeats in a serialized lifecycle; it does not
by itself establish correct tree ownership or thread-safe installation.

## What is still needed for late integration

- Observe whether native close/reopen really invokes the factory and the
  initialization entry, or reuses the existing view/tree.
- Identify the owning UI thread and an actual safe point for patch installation.
- Do not call +0x301828 on an existing live view as a shortcut: it allocates and
  reinitializes state and can leak/discard existing ownership.
- Only after demonstrating native reconstruction can the existing pre-init
  insertion wrapper serve as the late-load catch-up mechanism. Otherwise a
  separate initialized-tree migration/repopulation implementation is required.

No claim that late loading is now supported follows from these static facts.

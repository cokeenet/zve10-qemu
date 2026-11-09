# Explicit late-load discovery probe

This DSO has no constructor, exported dlopen interposer, hook installer, TLS,
worker thread, file access or persistent log. The caller explicitly invokes
nm_discovery_probe. It enumerates loaded modules, obtains RTLD_NOLOAD handles,
uses handle-scoped dlsym and verifies symbol owners with dladdr. Handles are
retained as module references and released before another explicit probe.
It is a serialized probe API, not a concurrent monitoring API.

It reports module presence and a narrow anchor check. It does not patch code,
insert a menu, reconstruct an initialized tree, call into UI lifecycle methods,
or establish that a remote thread can safely invoke dlopen. On a real UI, acquiring
handles runs loader operations, so it is not equivalent to a passive memory read.

Host test:

```
python3 test_probe.py
```

Loads synthetic Obj/view/Caution DSOs with RTLD_LOCAL, verifies their view symbol
is not globally visible, creates three threads, then late-loads probe.so under
RTLD_LOCAL and RTLD_GLOBAL in separate processes. Both cases call the probe
repeatedly and assert module handles/bases. A synthetic Caution constructor is
not placed at the real firmware offset, so its anchor bit is not asserted.

The same harness passed under ARM Cortex-A9 QEMU with the camera's glibc:

```
LATE_DISCOVERY_PASS
ARM_DISCOVERY_RESULT mode=local status=0
LATE_DISCOVERY_PASS
ARM_DISCOVERY_RESULT mode=global status=0
```

See arm-result.txt and arm-manifest.json for markers and complete log/ELF/cpio
identities. The final systemd unit used MemoryMax=2G, RuntimeMaxSec=65 and was
explicitly stopped; no QEMU remained. An earlier launch failed before QEMU
started due to a mistyped stdout directory and was not counted as a test result.

The fixture does not use Sony UI libraries. Real camera acquisition for this
investigation was limited to /proc maps/status and library PULL/hash comparisons;
no ptrace attach or runtime UI modification took place.

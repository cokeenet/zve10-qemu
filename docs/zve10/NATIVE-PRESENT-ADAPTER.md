# Experimental slim presentation adapter

## Scope and hardware status

Local source and host/ARM compiler checks only. This work did not replace the
camera library, restart the UI, reboot, or modify snapshot storage. The adapter
is disabled unless NM_EXPERIMENTAL_NATIVE_PRESENT is defined at compile time.
It is explicitly startup-preload-only, not a supported late-dlopen path.

## Established boundary

Existing full/observer-derived.c replaces libObj+0x774ef0 with software_present.
That implementation copies a captured tiled surface in software, then invokes
native_entry_present. It does not establish that an actual GPU operation is
complete when the original submit function returns.

A bounded GNU ARM objdump inspection of local libObj.so (SHA256
cfdc59b94fa56116e9d99154ca03a2c1aaf6039dd272679b78b0b76e21c722ac)
showed this sequence at the captured render call site:

```
0x777930  call 0x774ef0 (submit)
0x777934  r0 = 2
0x777936  call 0x775120 (wait engine 2)
```

The wait function has an ioctl/status loop and uses the 0x40000000 completion
bit for this engine. This is static assembly evidence from ordinary objdump,
not a runtime GPU observation or REA decompilation. REA's native deep engines
were unavailable in the connected session; no target was opened.

## Local implementation

native_present_adapter.h keeps original submit and wait calls, records only the
exact captured 640x480 RGBA4444 ABI/caller, and dispatches overlay after original
engine-2 wait returns with completion bit set. Thread-local pending metadata avoids
consuming another thread's submission. Invalid format/caller, unrelated engine,
inactive overlay and missing completion preserve the original return value without
painting. Pending metadata is consumed once per completed submission.

native_present_install.inc validates twelve-byte pins in executable file-backed
segments. Submit displaces 12 bytes. Wait displaces only 8 bytes: the next 4 bytes
include a relative branch and must not be copied by the generic 12-byte trampoline.
Both trampoline pointers are published before startup patching. Live concurrent
patching is unsupported. Mapping/protection failures are reported rather than
terminating the UI deliberately.

On a valid overlay completion, the destination physical surface is mapped through
/dev/mem, native_entry_present is called, memory ordering is enforced, then the
mapping is released. This assumes the captured destination layout is also the
native hardware layout. That assumption still requires controlled guest and
hardware evidence.

## Validation completed

- test_native_present.py compiles the actual adapter with mocked original
  boundaries. It verifies submit preservation, wait-before-overlay order,
  engine/format/caller guards, one-shot consumption, overlay-off behavior and
  different-thread isolation.
- test_title_dispatch.py still passes.
- Firmware PT_LOAD mapping and both twelve-byte prologues match.
- ARM object and isolated shared library linking succeed.
- Initial TLS implementation imported __tls_get_addr and incorrectly introduced
  ld-linux-armhf.so.3 from the cross toolchain. Startup initial-exec TLS removes
  that dependency. Final DT_NEEDED is libdl.so.2, libpthread.so.0, librt.so.1,
  libc.so.6 only. This is a compatibility check, not execution on camera glibc.

Experimental link output is isolated under /tmp/newmenu-present-audit-f8UfsY/;
the normal local libnewmenu.so and camera files were not rebuilt or replaced.

## Unverified conditions

- ARM/QEMU CPU execution now passes the mocked-boundary behavior test and the
  production installer/trampoline fixture, using camera glibc. See
  `tools/zve10/menu/tests/arm-present/` for sources, raw-log identities and zero
  exit statuses. This still does not execute the adapter against the firmware UI
  or GPU; that integrated run remains unverified.
- No real GPU cache-coherency or presentation visibility measurement.
- Completion semantics, buffer ownership until downstream consumption, physical
  address restrictions and frame format must hold on the actual hardware path.
- Current menu installer can partially install unrelated hooks. This adapter
  does not make that broader transaction atomic.
- Late-load insertion after menu construction remains unsolved.
- Current hardware startup appears to use snapshot resume; whether the UI process
  reruns loader initialization on ordinary reboot remains unproved per PID.

These gaps prevent promoting the experimental flag to the camera build. Required
acceptance remains: native row caption, ENTER opens the menu, MENU returns.

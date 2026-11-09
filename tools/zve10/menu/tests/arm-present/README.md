# ARM presentation adapter fixture

Result: both test child processes exited zero on QEMU `-M zve10`, Cortex-A9,
using the camera's `ld-2.11.2.so` and libc/pthread libraries. This exercises ARM
callback code, TLS and emitted Thumb trampolines. GPU routines are synthetic;
there is no firmware UI, menu screenshot or hardware GPU validation here.

- `fixtures.S`: exact 12-byte submit/wait prologues followed by synthetic returns.
  The wait body retains the relative branch at +10 and tests its taken path.
- `trampoline_test.c`: uses the production adapter/installer headers and the
  `ne_jump` body extracted for this test. It compares original and patched calls,
  validates the original-return trampoline and refuses a second installation.
- `runner.c`: PID 1 supervisor starts the two tests, reports wait statuses to
  `/dev/kmsg`, then remains alive. Exiting a test used as PID 1 causes a kernel
  panic and must not be mistaken for a test result.
- `result.txt`: extracted guest markers; both status values are zero.
- `manifest.json`: SHA256 identities and locations for complete raw log, test ELF
  files and the independent 2.8 MB cpio on the build machine.

The behavior-test C source was generated from `test_native_present.py`'s HARNESS
with a success marker added. The other source was generated from the supplied
trampoline fixture, replacing the production `ne_jump` placeholder with the exact
current function body. Re-extract the function when changing it; this saved source
is historical evidence, not a second implementation to maintain.

Cross-compile with the installed armv7-eabihf Buildroot 2020.08-1 compiler,
`-O2 -marm -I<menu-dir> -Wl,--dynamic-linker=/lib/ld-linux.so.3`, adding
`-lpthread` for the behavior test and `fixtures.S` for the trampoline test.
This fixture's interfaces use integers/pointers only. The compiler's default ABI
was used because its sysroot lacks softfp stubs. This does not qualify arbitrary
floating-point interfaces against camera softfp libraries.

Rootfs includes camera `/lib`, BusyBox, test ELF files and device nodes
`/dev/console` (5:1), `/dev/kmsg` (1:11). It is packed into an independent newc
archive. The final guest starts with `rdinit=/tests/runner`.

The final run used the existing reference kbuild-ui Image, patched route-A QEMU,
`-smp 4 -m 1024`, an external initrd, and the existing memory reservations.
Each run was started with `systemd-run --collect --no-block`, MemoryMax=2G and
RuntimeMaxSec<=100. The final unit was explicitly stopped. All five attempted
units were inactive afterwards and no qemu-system-arm process remained.

Initial shell/console attempts did not yield observable test markers. A direct
test-as-PID1 attempt exited and panicked; it was not classified as success. Only
the final supervised `/dev/kmsg` run establishes the reported passes.

Hardware library and deployment configuration were not changed by this fixture.

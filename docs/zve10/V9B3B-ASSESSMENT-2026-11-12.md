# v9b3b assessment: verified file-level, one real problem found

Checked the claims against the image itself with debugfs (no full mount,
no full-image read into memory).

## Claims verified true

- All three Gen5 core libraries match the stated hashes exactly:
  `libObj.so 4cd654f1…` (3,931,624 B), `appFw.so f08c9eb7…` (64,273,528 B),
  `appFwBoot.so 187b335c…` (8,312 B).
- `bootin.real` (15,336 B, md5 d8bfb4f7…) is byte-identical to the stock
  `bootin.elf` from v9 — the original is preserved and rollback is trivial.
- `im.elf` is stock (23,240 B, sha256 3fd399b6…).
- `mprshim.so` (7,200 B) and `libmpr.so` (3,038,540 B) are present, as are
  `LeakTracer.so` and the jemalloc tools under `/tool`.
- The wrapper script at `/bin/bootin.elf` does what the notes said: sets
  `LD_PRELOAD`, mirrors it into `/setting/mode/preload`, runs a two-stage
  background diagnostic (25 s maps/comm snapshot, +65 s thread stacks and
  dmesg), installs the g_mass_storage gadget if `appFw.so` never maps, then
  polls `/tmp/cmd.sh` once per second for 24 h as a root command channel, and
  finally `exec`s `bootin.real` so init's waitpid semantics are unchanged.

## The one real problem: the preload list is wrong for bootin's own gate

The wrapper exports and mirrors:

    /usr/tool/LeakTracer.so:/usr/lib/mprshim.so:/usr/lib/libmpr.so

But `bootin.real`'s `/setting/mode/preload` path is whitelisted at
`0x0A8DC[1..4]` to exactly four entries (LeakTracer, libefence,
libjemalloc_tsh, libmcheckb). `mprshim.so` and `libmpr.so` are not on it, so if
`bootin.real` takes the flag&0x40 path it will strip the file-provided preload
back to the whitelist. The direct `export LD_PRELOAD` path is not filtered, so
the environment variable survives — but that only reaches `im.elf` on a
non-snapshot boot, which is exactly the condition we are still trying to create.

## Also noted

- There is no `/usr` inside the image (the image root becomes `/usr` when
  nflasha15 is mounted), so `/bin/bootin.elf` is `/usr/bin/bootin.elf` — the
  path init execs. The shebang chain is valid: init execve -> binfmt_script ->
  busybox sh -> script -> exec bootin.real.
- A block-level diff against the stock 2.03 image is meaningless (every 4 MiB
  block differs) because the ext2 metadata was rebuilt; sizes differ by 3,072 B.
  File-level identity of the components above is the correct comparison basis.
- Rollback anchor: `/root/fw/zve10_backup/nflasha15.img` (314,572,800 B,
  md5 cdec7ad4…), complete and verified.

## Verdict

v9b3b is the right variant **if** we accept the gadget+cmd.sh channel and fix
the preload list to match bootin's whitelist (or rely only on the export path).
For a pure stock restore it is the wrong artifact — the stock image from the
official package is what belongs on the partition.

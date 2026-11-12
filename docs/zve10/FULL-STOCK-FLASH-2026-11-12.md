# Full stock flash through fastboot: done and verified

## What was written

Straight `flash_partition.py` runs against the official 2.03 package, with
`--part-start` set to each partition's real byte offset from `partinf.conf`:

| partition | start | image bytes | pieces | result |
|---|---|---:|---:|---|
| nflasha15 (/usr) | 0x2D800000 | 249,105,408 | 317 | byte-exact |
| nflasha7 (/rootfs) | 0x0A400000 | 5,132,288 | 7 | byte-exact |
| nflasha3 (/system) | 0x01C00000 | 30,157,824 | 39 | byte-exact |
| nflasha5 (WBI) | 0x05800000 | 36,039,424 | 46 | byte-exact |

Every piece was read back during the write, then the whole of each partition was
streamed back afterwards and compared with the image. All four are byte-exact.

## The mistake worth remembering

The first attempt omitted `--part-start`, so the host-side readback added
`part_start_lba = 0` and compared device LBA 0 against the ext2 superblock. It
raised `readback mismatch at 0x0 (piece 0)` and looked like a failed or
misdirected write.

It was not. `stage2` resolves the partition by name and writes at
`p->start_bytes` itself, so the data had landed correctly all along; only the
host's verifier was wrong. Two things confirmed that before touching anything
else:

- device LBA 0 still held `82461.00` (the eMMC header) and LBA 1 was zero, i.e.
  the boot-sector area was untouched;
- reading 768 KiB at LBA 0x16C000 (0x2D800000/512) matched the stock image head
  byte for byte.

`fetch_in_part()` in that tool needs `--part-start` for its own reads; without it
the verification is meaningless even though the write is fine.

## State after the flash

- `/usr/lib/CautionConfig.so` is back to stock `5ad8746a…`, so the 5 GHz page
  patch is gone with the partition. That is expected: it was a /usr content
  change.
- `/usr/lib/libnewmenu_usr.so` no longer exists, so `/etc/ld.so.preload` was
  edited down to the single `/etc/zve10_agent.so` line. Left alone it printed a
  loader warning for every exec on the camera.
- The agent survived, because `/etc` and `/setting` were deliberately not
  touched. TCP 7777 answers again.
- WBI on disk still hashes to `22a99b37…` = `WBI1`, 664 sections,
  `resume_vector 0x00124E1C`, `o_data_size 0x08234000`.
- `/setting/updater/` still holds `dat4` and `mode6`; the `mode` marker stays
  deleted, so the camera boots the normal system rather than p1.

## Still true, and the reason this did not change it

`im.elf` (pid 157) has starttime 104 while freshly exec'd processes sit in the
thousands, so this boot was still served from the retained image and `/usr` is
not re-read. `im.elf` carries zero `zve10_agent` mappings while `wpa_supplicant`,
`pump` and `busybox` carry four each. Flashing partitions fixes what is on disk;
it does not change which paths get executed. That is a separate problem and it is
still unsolved.

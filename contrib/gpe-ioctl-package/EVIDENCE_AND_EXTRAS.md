# Evidence and additional material available on request

Nothing below is shipped on the branch by default. Offer it selectively, and only after
confirming it fits the upstream distribution policy (no Sony firmware / binaries / copies).

---

## Our own probe output — safe to share as text

| Local file | Size | Content |
|---|---:|---|
| `work/KLOG_ioctl.txt` / `KLOG_ioctl2.txt` | 18 KB each | Early ioctl probe logs |
| `work/KLOG_ioctl3.txt` / `KLOG_ioctl4.txt` | 174 KB each | GPEW / GPEIRQ full trace |
| `work/KLOG_ioctl7.txt` | 644 KB | Extended trace |
| `work/qemu_gpe_err.txt` | 2.3 KB | `DMP_2D_Initialize : 0` evidence |
| `work/ko_gles_ioctl_r.txt` | 30 KB | Driver ioctl branch disassembly with relocations |

The 42 GPEW writes, 8 raises / 8 acks and the 1:1 kick/IRQ/ack ratio come from these logs.
`KLOG_ioctl5.bin` / `KLOG_ioctl6.bin` (1 MB each) are raw memory snapshots — **hold back**,
they may contain firmware-derived bytes.

## We offered these in the issue comment

| Item | Local file | Recommendation |
|---|---|---|
| GPE model patch complete diff | `GPE_MODEL_PATCH.md` §2 (inline C) | ✅ Shipped |
| GPEW / GPEIRQ log originals | `KLOG_ioctl3/4/7.txt` | Offer on request; text only |
| 2D command descriptor raw dump bytes | `work/gpe.bin` (16 KB) | ⚠️ Check provenance first — likely captured from guest memory. Offer as a hex excerpt of the repeating 8-word structure, not as a binary file |
| Full 17 ioctl annotations | `GRM_GLES_IOCTL_TABLE.md` | ✅ Shipped |

## Never ship

- `work/grm_gles.ko` — original Sony kernel module (16 KB)
- `work/fb_gpedst.bin` (1 MB) and the derived PNGs — framebuffer captures
- `dump_20261028/ptbl.bin`, `Backup.bin`, `nflasha15.img`, `zve10_dump_20261028.zip`
- any `.elf`, `.so`, `.ko`, `.img` or partition image from the donor firmware

## Additional original analysis we could contribute (not requested)

These are **our own written conclusions**, not firmware bytes, so they carry no
distribution problem:

1. **Sony private partition table format** — magic `82461.00` (`+0x00`), `u32 n = 30`
   (`+0x08`), `u32 ver = 1` (`+0x0c`), then 30 records of
   `{ u16 unk; u16 start_64k; u16 unk; u16 len_64k; u32 type; u32 valid; }` at `+0x10`.
   Verified record-by-record against `/proc/partitions`. May be useful to the fastboot /
   eMMC partition-write path.
2. **`osal_uipc.ko` cmd 0–16 semantics** and the 11,389-call IPC statistics — already in PR #3.
3. **ZV-1F as a QEMU guest: three pitfalls** — in `GPE_MODEL_PATCH.md` §5.
4. **Real-camera read-only capture constraints** — `contrib/REAL_CAMERA_FINDINGS.md`;
   note the LZP/`usr` claim there is superseded by the upstream issue #4 clarification.

## Suggested sequence

1. Push this package to our branch (or paste Version B of the reply).
2. Wait for their model result. If they still hang, offer `KLOG_ioctl3/4.txt` and the
   hex excerpt of the 2D descriptor.
3. Only then raise the partition-table format as a separate, unrelated contribution.

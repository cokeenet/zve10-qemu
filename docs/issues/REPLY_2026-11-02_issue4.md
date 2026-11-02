Hi @qn06142,

Thank you — genuinely. This is exactly the kind of correction that saves other people weeks, and we owe you an apology for getting it wrong in the first place.

## What we did wrong

We read the LZP section of `BURIED_TREASURE.md` and treated it as authoritative. Worse, instead of leaving it as an unverified upstream claim, we built on top of it: we observed that `uz_emmc_do_compress` in `emmc.ko` is a 20-byte no-op stub (while `uz_emmc_do_decompress` right beside it is 2,784 bytes — we re-verified both just now: `0x5cf8`/20 and `0x601c`/2784, identical across the E10, M2 and 1F modules), and reasoned "no compressor ⇒ `/usr` cannot be written back ⇒ every write is discarded". The stub observation itself was correct; the conclusion we piled on top of it was not. We then propagated that conclusion into four separate documents, and into a safety rule that told readers there is no path where editing `/usr` and rebooting keeps the change.

Meanwhile our own measurements in `docs/EXTERNAL_ZVE10_RE.md` already said the opposite — `debugfs` on `zve10_delivery/nflasha15.img` shows `ext2 magic = 0xEF53`, a clean superblock, and a fully readable file tree. We had the right answer in one file and the wrong one in four others. That is our failure to reconcile our own notes, not any problem with yours.

**All of it is now retracted and pushed** (commit `9ef4ada`). Each of the four affected documents carries a retraction notice citing your clarification; the safety rule is kept but re-justified on the genuine hazards (mapped `.so` files not releasing space on overwrite, `O_TRUNC` leaving half-written files — we destroyed a `libObj.so` that way); `docs/EXTERNAL_ZVE10_RE.md` now has a clarification summary table so the two halves of the repository cannot contradict each other again.

One further thing we are retracting while we are at it: we had also recorded a real-device experiment where overwriting `bootin.elf` appeared to revert on reboot while a newly created file survived, and we had cited that as evidence for non-persistence. That experiment was also wrong — we got something wrong in how we ran it, and the observation does not reflect the filesystem's behaviour. We have marked it void in the repository and removed it from our reasoning. There is no open anomaly: `/usr` on `nflasha15` is plain uncompressed ext2, and writes persist.

## Thank you for the rest, too

The load-bearing parts of your message for us:

- **`/usr` persistence confirmed** — this changes our deployment options materially. We had effectively ruled out persistent `/usr` edits and were designing only around in-memory `--bind` overlays and preload hooks. It also means the failure mode is far more dangerous than we had it recorded: a bad write is remembered, not discarded.
- **`wdt.mode=1 xrstreq=1` + 500ms watchdog** — matches the self-correction we pushed on 2026-10-23 after reading `/setting/KEMCO.TXT` and a live `/proc/cmdline`. Good to have it confirmed by someone with the hardware in hand, and the BootROM-clears-`TIMER-IP` explanation resolves the apparent contradiction with our `WDT TIMERCTL=0` reading.
- **`0x20312000` is a descriptor table, not CPU-visible scanout** — independently matches our `REALCAM_GROUND_TRUTH_2026-10-27.md` §7. We had already stopped treating it as a pixel surface, but two independent confirmations beat one.

And thank you for the RP2040 BootROM / USB-C serial injection comment. That lane exists precisely because we bricked a unit early on and needed a way back that did not depend on the system booting, so it is good to hear it looks useful from the outside.

## On your dump offer

**Yes please, and thank you.** We would rather not have you spend effort blindly, so here is what would move us forward most, in priority order:

1. **`/setting` full contents from a device that has booted normally at least once** — especially `Backup.bin`. On our side that is currently a hard wall: `backup.ko` must `file_load` a valid `/setting/Backup.bin` at insert time, the firmware package does not ship one (the `0800_appli/setting` payload contains only `DmmConfig.bin`, `facedmy.bin`, `kemco.txt`, `ulogio.bin`, `appfw_infra`), so it is evidently generated on first boot. Without it the backup service never answers and our boot stops at the same point every time; with `Backup_load=0` insertion fails outright (`insmod ... Input/output error`, status 5) and takes `ulogio`/`dmm`/`udate` down with unresolved `Backup_read*` symbols.
2. **The first 64 KB of an `nflasha` user-area image** (hex or raw) — to compare against `partinf.conf`.
3. If it is cheap: the **`/version.txt`** and **`ulogio.bin`** from the same device, so we can confirm which generation pairs with which backup database.

We will not publish any of it, and we will describe findings without redistributing your binaries.

For what it is worth, our current blocker is on the graphics side and we know it: the application registers `sugilite_interrupt` on IRQ 76 and reads status at `bar + 0x34`, and we only have a minimal emulated device. It delivers interrupts and the driver even submits a handful of fence packets, but the ioctl waiting on the completion bit never returns, and forcing it open aborts the application. That is a device-model problem on our side, not anything from your notes.

Thanks again for taking the time to write this up properly instead of letting us stay wrong. Corrections like this are worth more than new findings.

---

Best regards,
**cokeenet**

*Maintainer, [cokeenet/zve10-qemu](https://github.com/cokeenet/zve10-qemu)*

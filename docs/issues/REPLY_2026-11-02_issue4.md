Hi @qn06142,

Thanks for the correction — we got it wrong.

We had taken the LZP section of `BURIED_TREASURE.md` at face value and even reasoned on top of it (`uz_emmc_do_compress` is a 20-byte stub, so "writes can't persist" — the stub is real, our conclusion wasn't), then spread that into four documents. Meanwhile our own `debugfs` notes already showed `ext2 magic = 0xEF53` on `nflasha15`. We had the right answer in one file and the wrong one in four.

**All fixed and pushed** (`9ef4ada`): retraction notice in each affected document, the safety rule keeps its verdict but now cites the real risks instead of a wrong reason, and `EXTERNAL_ZVE10_RE.md` has a summary table so the repo can't contradict itself again. We also voided a related experiment of ours that was simply done wrong — not an open question, just our mistake.

The three confirmations you gave were exactly what we needed: `/usr` persistence (changes our deployment options), the 500ms watchdog (matches our 10-23 self-correction), and `0x20312000` being a descriptor table rather than scanout (matches our §7). Thanks for taking the time to write it up instead of leaving us wrong — this is worth more than new findings.

Full disclosure: my English isn't great, so I drafted this with AI help — sorry for anything that reads oddly.

Also, if you ever feel like hanging out: **https://discord.gg/49T7pVRnfB** — our little camera-RE corner. Fair warning, day-to-day chat is mostly Chinese, but plenty of us read English fine and we'll switch for you. No pressure at all. 🙂

---

Best regards,
**cokeenet**

*Maintainer, [cokeenet/zve10-qemu](https://github.com/cokeenet/zve10-qemu)*

# ZV-E10 RE

**English** · [简体中文](README.zh-CN.md)

Interoperability research on the **Sony ZV-E10** (ILCE-ZV-E10, CXD90045 / "ASTRA" SoC, ARMv7 soft-float):
hardware-level rescue, firmware reverse engineering, and running the stock firmware inside QEMU.

[![Status](https://img.shields.io/badge/status-active-brightgreen)](#project-status)
[![Platform](https://img.shields.io/badge/platform-ZV--E10%20%7C%20CXD90045-blue)](#)
[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-orange)](LICENSE)

> [!WARNING]
> **This repository contains no Sony firmware, binaries, certificates or copies thereof.**
> Only original glue, research notes, patches and derived technical data are published.
> You must dump firmware from your own device, and you are responsible for your own compliance.
> See [NOTICE.md](NOTICE.md) and [License](#license).

---

## Contents

- [What this is](#what-this-is)
- [Project status](#project-status)
- [Repository layout](#repository-layout)
- [Quick start](#quick-start)
- [Documentation](#documentation)
- [Safety](#safety)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgements](#acknowledgements)

---

## What this is

**The goal is one thing: porting the newer menu UI (ZV-1M2 / CXD90045_k8 generation) onto this body.**
Everything else here is either groundwork for it, a tool to iterate on it, or the safety net that lets
us survive getting it wrong.

| | Workstream | What it produces | Role | Status |
|---|---|---|---|---|
| **N** | **New menu port** — *the objective* | A `New Menu` row in the stock menu that opens the A7C2 menu; three frames in QEMU hash-identical to the reference | **The goal** | **Done in QEMU**; camera pending |
| **A** | **Hardware rescue / anti-brick** | BootROM serial injection, USB fastboot, raw eMMC read/write, single-byte repair | Safety net | **Closed loop on real hardware** |
| **B** | **Firmware RE / system mapping** | Update-package decryption and unpacking, partition map, JIRITSU channel, system topology, menu resource formats | Groundwork | **Main structures mapped** |
| **C** | **QEMU emulation** | Stock firmware booting and running the body application under emulation, with instrumented probes | **Iteration tool for N** | **Booting; display pipeline unfinished** |

The dependency runs downward: **B** explains what the system is made of, **C** lets us iterate on **N**
without touching hardware, and **A** is the only way back when something bricks. But every one of them
exists because of **N**.

## Project status

Honest snapshot — see the linked documents for evidence and logs.

### A — Hardware rescue ✅

- **BootROM serial injection** (Pico, `zve10_uart_od 15` protocol) → bare-metal fastboot payload (USB `18d1:4ee0`) → runtime-loaded stage2 eMMC driver.
- **Both reads and writes verified on hardware.** Writes were validated byte-for-byte by writing a pattern into dead space, reading it back, and restoring. Stage2 is read-only by default and refuses writes unless the device reached the TRAN state.
- **Real brick recovered.** A BPB-driven (no fixed offset) scan located the `ld.so.preload` directory entry, and clearing its size field — a net change of **one byte** — brought the UI back.
- **Watchdog characterised.** An application-armed TIMER-IP watchdog survives the XRESET_REQ injection pulse and resets the payload after roughly **3 min 40 s**; the payload disarms it via the kernel's own `WDT_SET_STOP` on entry. This behaviour appears to be unique to this model among the units we could compare.

Details: [docs/FASTBOOT_EMMC.md](docs/FASTBOOT_EMMC.md) · [docs/FASTBOOT_OPERATIONS.md](docs/FASTBOOT_OPERATIONS.md) · [fastboot/](fastboot/)

### B — Firmware RE / system mapping ✅

Full decryption and unpacking of the official update package, partition table reconstruction, module/service inventory, and the graphical stack. A notable finding: the UI's scene graph (`Render2DWalker`, `OffScreenRender3D`) is a **pure software traversal**, so drawing may not require emulating Mali at all.

Details: [docs/FIRMWARE_REPORT.md](docs/FIRMWARE_REPORT.md) · [docs/GEN1_SYSTEM_STRUCTURE.md](docs/GEN1_SYSTEM_STRUCTURE.md) · [docs/JIRITSU_CHANNEL.md](docs/JIRITSU_CHANNEL.md)

### N — New menu port ✅ *(the objective, in QEMU)*

Adding a **New Menu** row to the stock menu, which opens the A7C2 menu.

**Working.** Captured from this machine's QEMU, and the frames are byte-identical
to the upstream reference:

```
セットアップ5                                  5/5
  メディア残量表示  バージョン表示  設定リセット  プライバシー通知
  New Menu            <- orange highlight
                                              MENU 关闭
```

| our artifact | sha256 | upstream match |
|---|---|---|
| parent page PNG | `93ee3a7e1428a3741d46a991a64aae82…` | `visible-parent.png`, `capture-006/007/015/016/019.png` |
| after ENTER PNG | `41aec88e71396669c1d65640892ad7cf…` | `visible-new-menu.png`, `capture-008/009/012/013/014/017/018.png` |
| after MENU PNG | `93ee3a7e1428a3741d46a991a64aae82…` | `visible-parent.png` |
| parent raw RGBA4444 | `a29e2aad216f77b710904a5540f41d60…` | `main-review.json` → `parent_raw_sha256` |
| after-ENTER raw | `0c7e1c6682ec4cde4c56e8e2b2b617b7…` | `main-review.json` → `host_category_sha256["7"]` |

So: the row appears, `ENTER` opens the A7C2 menu, `MENU` returns to the parent
page — and each frame hashes equal to what the reference implementation published.
The frames were produced both by upstream's prebuilt library and by one compiled
from this repository's own sources.

**How.** One `LD_PRELOAD` library hooks `dlopen`, then hooks
`CmnViewSettingNode::initSettingNode` in `CautionConfig.so` (reached as a
`DT_NEEDED` of `viewUnified2.so`), and appends a node to `parent->children` after
the original returns.

```sh
sh tools/zve10/menu/build.sh slim    # libnewmenu.so      135,332 B  camera
sh tools/zve10/menu/build.sh full    # libnativeentry.so  214,228 B  QEMU verification
```

**Delivery.** `/etc/ld.so.preload` naming `/etc/libnewmenu.so`. This was chosen
over `/setting/env.txt` because `env.txt` needs four conditions to hold at once —
`/setting` mounted in time, the `dmode` gate open, `set_oreore_env` actually
running, and the variable surviving `bootin`'s `execve` — and on hardware the
variable never arrived. `ld.so.preload` needs none of them.

```sh
sh tools/zve10/envfile/deploy_full.sh <camera-ip>
sh tools/zve10/envfile/rollback_ldsopreload.sh <camera-ip>   # rollback
```

**Hardware status.** Not yet verified — the camera is offline. The deployment
package is ready and its mechanics were verified in QEMU (a vfat `/etc` with a
`ld.so.preload` gets 89 processes to load the library, `im.elf` included).

**Six things that must stay fixed**, any one of which silently breaks the library:
`ne_modules_installed`, `ne_caution`, `ne_view_handle`, 12-byte hook anchors, no
`setitimer`, and four-way `dlopen` fallback. Explained in
[`tools/zve10/menu/README.md`](tools/zve10/menu/README.md).

**Start here:** [docs/zve10/README.md](docs/zve10/README.md) — status, evidence,
mechanism, and the eight traps worth not re-stepping in.

### C — QEMU emulation 🟡 *(tool for N)*

The stock firmware boots, loads its platform modules, and runs the body application (`im.elf`) without heap corruption or aborts.

| # | Milestone | State |
|---|---|---|
| 1 | Kernel boots → `init` → **33 platform modules** loaded | ✅ |
| 2 | Body app runs; `libObj.so` (UI main library) loads at **16.85 s** | ✅ |
| 3 | `liro` (4-MUSASHI image processor firmware) runs, spawning ~280 kernel threads | ✅ |
| 4 | HDMI / CEC driver init | ✅ |
| 5 | No heap corruption, no abort, no SIGABRT across runs | ✅ |
| 6 | **Display chain**: app → `/dev/dmpgles2` → `grm_gles.ko` → GPU | ❌ |
| 7 | Input chain (buttons / touch) | 🟡 modules load, not wired to UI |

Current blocker is understood and specific: the graphics driver registers a threaded interrupt on **IRQ 76** and reads status at `bar + 0x34`; our emulated device is minimal. It delivers interrupts and the driver even submits a handful of fence packets, but the ioctl waiting on the completion bit never returns. That is a device-model gap on our side.

Details: [docs/QEMU_M2_PROGRESS.md](docs/QEMU_M2_PROGRESS.md) · [docs/analysis/qemu/QEMU_BOOT_PROGRESS.md](docs/analysis/qemu/QEMU_BOOT_PROGRESS.md)

## Repository layout

```
fastboot/      BootROM injection + two-stage bare-metal payload + host flash/verify tools
  pico/          serial injection firmware and host-side injector
  host/          bootstrap / flash / verify / partition utilities
  prebuilt/      stage1 and stage2 binaries
rescue/        Bare-metal payload sources and serial-boot protocol notes
hardware/      BootROM, UART pads, LEDs, Type-C, probe notes
display/       GRM interface inventory, recording stub, display-path trade-offs
emulator/      QEMU machine model work and menu experiments
modpkg/        Firmware module packages (minimal / replacement sets)
patches/       kernel/ and qemu/ patches, plus reusable trace snippets
scripts/       Build, run, and guest-shell helpers
tools/         Porting feasibility: ABI closure, xrefs, IMDB/UXAC parsing
data/          Derived tables: partition map, IMDB table, model comparison
guard/         LD_PRELOAD observation probes
docs/          Research documents — start at docs/README.md
```

## Quick start

Everything below assumes you already have your own firmware dump and the toolchain the scripts expect.

```sh
# Emulation
./scripts/build_patched_emmc_safe.sh     # build a patched disk image
./scripts/run_qemu_zve10.sh              # boot it
./scripts/shell_zve10.sh "cmd"           # guest root shell (most useful debugging tool)
./scripts/shell_camera.sh "cmd"          # mount + chroot into the camera rootfs
```

Hardware operations (read the operational manual first — these touch your camera):

```sh
# docs/FASTBOOT_OPERATIONS.md
cd fastboot && make build
```

## Documentation

Start at the **[documentation map](docs/README.md)**. The pages most people need first:

| Document | Why |
|---|---|
| [docs/zve10/README.md](docs/zve10/README.md) | ★ **start here** — current status, hash evidence, mechanism, eight traps |
| [docs/NEWMENU_STATUS.md](docs/NEWMENU_STATUS.md) | Earlier status (2026-11-05). The three CP blocker analyses still hold; "no frame yet" is superseded |
| [docs/README.md](docs/README.md) | Index of all 300+ documents, grouped by topic |
| [docs/STATUS_20261023.md](docs/STATUS_20261023.md) | Status snapshot and next-session checklist |
| [docs/FASTBOOT_OPERATIONS.md](docs/FASTBOOT_OPERATIONS.md) | Pico injection, wiring, stage2, verified read/write procedure |
| [docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md](docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md) | Brick analysis and recovery playbook |
| [docs/SAFETY_RULES.md](docs/SAFETY_RULES.md) | Operating rules for writing to the camera's partitions |
| [docs/ANTIBRICK_SYSTEM.md](docs/ANTIBRICK_SYSTEM.md) | Anti-brick design and recovery envelope |
| [docs/GEN1_BOOT_HARDWARE_TYPEC.md](docs/GEN1_BOOT_HARDWARE_TYPEC.md) | Boot-bridge hardware |
| [docs/EXTERNAL_ZVE10_RE.md](docs/EXTERNAL_ZVE10_RE.md) | Cross-reference against external ZV-E10 research |
| [docs/ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md) | Prior work this builds on |

## Safety

> [!CAUTION]
> The hardware scripts run as **root on your camera** and write to partitions. There is a real chance of producing a non-booting device.

- **Never** point a preload mechanism at a library that fails to load — the shell itself will stop starting, and software-level rescue becomes impossible.
- The write path is persistent, not discarded: a bad write is remembered across reboots. Keep a verified full backup before touching anything.
- Read [docs/SAFETY_RULES.md](docs/SAFETY_RULES.md) before your first write.

## Contributing

Issues and pull requests are welcome. Especially useful:

- experience with **software rasterisation / scene-graph rendering** (the UI path may be CPU-only)
- **Mali / SUGILITE** device modelling for QEMU
- porting feedback from other **CXD90045 / BIONZ** bodies

If you are reporting a QEMU issue, include the serial log and the exact commit you built from.

## License

[PolyForm Noncommercial License 1.0.0](LICENSE). Noncommercial use only; see [NOTICE.md](NOTICE.md) for the scope of what is and is not distributed here.

Trademarks and product names belong to their respective owners. This project is not affiliated with Sony.

## Acknowledgements

This work stands on a lot of public research. See [docs/ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md) for the full list.

- **Lost-week-1025** — CXD90045-FPSU
- **DavidBuchanan314** — ILCE-7M4-RE, Sony-ILCE-7M4-Linux
- **ma1co** — Sony-PMCA-RE, PMCA-QEMU, `fwtool` (upstream maintainer; originally from nex-hack)
- **chocotan** — a7c2-re (reverse engineering) and a7c2-qemu (QEMU machine model)
- **YuuLuo** — a7c2-service-preload (service mode / `ld.so.preload` prior work)
- **qn06142** — zv-e10-re (firmware 2.0x persistence and boot-argument findings)
- **dino2048** — zv-e10-toolkit
- **ChambersXDU** — sony-a7c2-research
- **ice21415** — a6000-firmware-platform
- **BI2QFA** — a6300_updater_emu
- The QEMU community, and everyone who has published camera reverse-engineering notes

---

Community chat (mostly Chinese, English speakers welcome): **https://discord.gg/49T7pVRnfB**

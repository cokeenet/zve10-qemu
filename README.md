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

Three independent workstreams, all on the same camera:

| | Workstream | What it produces | Status |
|---|---|---|---|
| **A** | **Hardware rescue / anti-brick** | BootROM serial injection, USB fastboot, raw eMMC read/write, single-byte repair | **Closed loop on real hardware** |
| **B** | **Firmware RE / system mapping** | Update-package decryption and unpacking, partition map, JIRITSU channel, system topology | **Main structures mapped** |
| **C** | **QEMU emulation** | Stock firmware booting and running the body application under emulation | **Booting; display pipeline unfinished** |

They are complementary rather than sequential: A is the only path back when the system cannot boot,
B explains what the system is made of, and C lets you iterate without touching hardware.

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

### C — QEMU emulation 🟡

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
- **ma1co** — Sony-PMCA-RE, PMCA-QEMU, `fwtool.py`
- **chocotan** — a7c2-re (native QEMU camera experiments, software GPE / ramfb approach)
- **YuuLuo** — a7c2-service-preload (service mode / `ld.so.preload` prior work)
- **qn06142** — zv-e10-re (firmware 2.0x persistence and boot-argument findings)
- **dino2048** — zv-e10-toolkit
- The QEMU community, and everyone who has published camera reverse-engineering notes

---

Community chat (mostly Chinese, English speakers welcome): **https://discord.gg/49T7pVRnfB**

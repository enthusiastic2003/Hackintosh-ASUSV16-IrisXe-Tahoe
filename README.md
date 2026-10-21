# Iris Xe acceleration on macOS Tahoe — ASUS Vivobook V16 (V3607VH)

Hardware-accelerated graphics (**Metal, Core Animation, Liquid Glass**) for the **Intel Graphics of the Core 7 240H (Raptor Lake-H, 8086:A7AB, 64 EU)** on **macOS Tahoe 26.7.1**, in an OpenCore hackintosh.

There is no Apple driver for Raptor Lake. This project runs **Apple's own driver for Tiger Lake Iris Xe** (GPUDriversIntel 16.0.32, written for Macs that never shipped) on the newer GPU. Both GPUs share the Xe-LP (Gen12) architecture. A modified **NootedGreen** Lilu plugin bridges the generation gap in the kernel, and a set of user-space patches bridges the gap between that 2020-era driver and Tahoe's Metal stack.

> 🚧 **Work in progress — this is a port, and it has not been booted yet.** The GPU stack was developed and verified on
> a different laptop (the upstream project this was forked from); everything marked ✅ below was confirmed there, not on
> the Vivobook V16. Expect changes as the first boots on this machine are debugged; see
> [CHANGELOG.md](CHANGELOG.md) and [docs/ASUS-V16.md](docs/ASUS-V16.md) for what is known and what is a guess.

> 🇧🇷 Leia em português: [README.pt-BR.md](README.pt-BR.md)

> ⚠️ **Experimental.** Expect occasional GPU hangs and kernel panics. Keep a second bootable disk (or the USB installer) around. Read [docs/STATUS.md](docs/STATUS.md) before you start.

---

## What works

> Confirmed on the reference laptop this stack was developed on (96-EU GPU, 3840×2400 panel). **None of it has been
> verified on the Vivobook V16 yet** (64-EU A7AB, 1920×1200 panel); treat every row as the goal, not a result.

| Area | Status |
|---|---|
| Desktop, Dock, windows, Liquid Glass blur | ✅ GPU-accelerated through Metal |
| Native panel resolution and HiDPI scaling | ✅ on the reference 3840×2400 panel (switch with `tools/xe-res`, not System Settings); the V16's 1920×1200 panel is untested |
| GPU clock | ✅ 1100 MHz floor while busy (`ngreengtfreq`), idles in RC6 |
| HEIC / YUV images (wallpapers, photos) | ✅ |
| App icons, Finder icons, image thumbnails | ✅ |
| YouTube | ✅ in Chrome (GPU on, up to 4K) · ✅ Safari with an audio output (Bluetooth or the BlackHole virtual device, `tools/build-blackhole.sh`) |
| Screen recording | ✅ |
| Hardware video decode (H.264/HEVC) | 🚧 in progress (off by default; video plays decoded on the CPU) — see [docs/STATUS.md](docs/STATUS.md) |
| Brightness keys | ❌ use `tools/xe-brilho` |
| System Settings › Displays | ⚠️ may panic, change resolution with `tools/xe-res` |
| WindowServer hang | ⚠️ occasionally before login, and sometimes when changing the UI scale (reboot recovers) |

Everything else about the Vivobook V16 is a regular OpenCore setup (keyboard/touchpad via VoodooPS2/VoodooI2C, NVMe, battery, USB map, AppleALC audio on the Realtek ALC256 — all taken from an EFI that booted Sequoia on this laptop). The Realtek 8852BE Wi-Fi and the Realtek USB Bluetooth have **no macOS driver** (use ethernet or a USB adapter), the RTX 5050 is disabled, and the camera is not supported.

## How it works (one paragraph)

The EFI presents the GPU as Tiger Lake (`device-id 9A49`). Apple's TGL kexts load from `/Library/Extensions`, and **NootedGreen** patches them in memory: display engine 13 quirks, the GuC-less host scheduler, broken resolve paths, IOSurface layout changes in Tahoe, GPU frequency, and more ([docs/KERNEL-PATCHES.md](docs/KERNEL-PATCHES.md)). In user space, the TGL **Metal driver** is binary-patched and gets an adapter library (`libxedrv`) that converts Tahoe's private Metal structures to the old layout and works around hardware limits. The TGL **shader compiler** (IGC gen12lp, LLVM 3902) runs inside Tahoe's compiler service (LLVM 32023) through an API adapter and an LLVM bridge ([docs/METAL.md](docs/METAL.md)).

## Repository layout

```
EFI/                     OpenCore EFI for the Vivobook V16 (serials removed — generate your own)
  OC/config.plist        accelerated config
  OC/config-install.plist  safe config for installing macOS (NootedGreen off, verbose)
src/NootedGreen/         the modified NootedGreen source (Lilu plugin)
build-nootedgreen.sh     builds src/NootedGreen with only the Command Line Tools
patches/                 diff of this fork against upstream NootedGreen 95f8189
macos/                   user-space stack: Metal driver patcher, libxedrv, shader compiler, installer
tools/                   xe-res (resolution/DPI), xe-brilho (brightness), xe-perfil (color profile),
                         XeShim + xectl (register access), diagnostics/ (the test programs used to find bugs)
docs/                    install guide, patch catalogs, status, debugging notes
```

Documentation: [INSTALL](docs/INSTALL.md) · [STATUS](docs/STATUS.md) · [KERNEL-PATCHES](docs/KERNEL-PATCHES.md) ·
[METAL](docs/METAL.md) · [DEBUGGING](docs/DEBUGGING.md) · [HISTORY](docs/HISTORY.md) (how it was done, phase by phase) ·
[EXPERIMENTS](docs/EXPERIMENTS.md) (what failed and why) · [CATALOG](docs/CATALOG.md) (full patch and tool reference)

## Quick start (ASUS Vivobook V16, V3607VH)

**1. Make your EFI** (on any Mac, with git):

```sh
git clone https://github.com/enthusiastic2003/Hackintosh-ASUSV16-IrisXe-Tahoe.git && cd Hackintosh-ASUSV16-IrisXe-Tahoe
zsh make-efi.sh        # -> dist/EFI with YOUR OWN serial numbers (never share them)
```

**2. Install macOS Tahoe 26.7.1 (25G241)** booting with `EFI/OC/config-install.plist` renamed to `config.plist`
(acceleration off). BIOS: usual hackintosh settings (storage in AHCI/NVMe mode, not RAID/VMD; Secure Boot off).

**3. On the new macOS**, with the accelerated `config.plist` back in place:

```sh
xcode-select --install          # Command Line Tools, once
zsh install.sh                  # checks, downloads Apple's driver bundles via NootedGreen, builds, installs
```

Copy the freshly built `build/NootedGreen.kext` to `EFI/OC/Kexts/` (the one in the repo is stale and still assumes a
96-EU GPU), approve the extension if asked, reboot.
`zsh install.sh --check` only runs the checks; `zsh install.sh --uninstall` removes everything.

Other hardware (other Raptor Lake / Alder Lake laptops): the user-space part is the same, but you need your own
EFI (ACPI, USB map, input devices). The full manual procedure is in [docs/INSTALL.md](docs/INSTALL.md).

The laptop-specific details (what was changed for the A7AB, what is still a guess, bring-up order) are in
[docs/ASUS-V16.md](docs/ASUS-V16.md).

## Credits

- **NootedGreen** by StezzaPilot ([sgiammori/NootedGreen](https://github.com/sgiammori/NootedGreen)): the base of the kernel plugin and the source of the 16.0.32 driver bundles. Licensed under the *Thou Shalt Not Profit License 1.0*, which also covers the fork in `src/`.
- **WhateverXe** and **OpenCore Legacy Patcher fork** by b00t0x: reference for Gen12 on macOS.
- **IntelIGPU-Tahoe**: reference for running the TGL Metal driver on Tahoe.
- **acidanthera** (OpenCore, Lilu, WhateverGreen, VirtualSMC, MacKernelSDK), **OpenIntelWireless** (itlwm, IntelBluetoothFirmware), **VoodooI2C**, **USBToolBox**.
- Forked from [JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe), where this GPU stack was developed and verified (pair-programming with Claude, Anthropic). The Vivobook V16 port was done the same way.

## License

[Thou Shalt Not Profit License 1.0](LICENSE), the license of NootedGreen, which this project derives from. Third-party
kexts in `EFI/` keep their own licenses, and no Apple binaries are included: see [NOTICE.md](NOTICE.md).

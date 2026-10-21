# ASUS Vivobook V16 (V3607VH) — Core 7 240H, Intel Graphics `8086:A7AB`

Port of the Iris Xe work to a second machine. **Nothing here has been booted yet**: it is the analysis and the changes
made from Windows on the target laptop, to be confirmed on the first macOS boot. Treat every item as *untested*.

| | Reference laptop (upstream) | ASUS Vivobook V16 V3607VH |
|---|---|---|
| CPU | i7-1360P (Raptor Lake-P, 4P+8E) | Core 7 240H (Raptor Lake-H refresh, 6P+4E) |
| iGPU | `8086:A7A0`, 96 EU (6 DSS) | `8086:A7AB`, **64 EU (4 DSS)** |
| Panel | 3840×2400, 4-lane eDP | CSOT `MNG007QX2-2`, 1920×1200 @ 60 Hz (EDID range 48–144 Hz), 162.24 MHz |
| dGPU | none | RTX 5050 (`10DE:2D98`, behind PEG1 `00:01.0`) |
| Wi-Fi / BT | Intel (itlwm, IntelBluetoothFirmware) | Realtek 8852BE / Realtek USB BT — **no macOS driver** |
| Touchpad | I2C-HID on the reference laptop's ACPI paths | I2C-HID `ASCP1201` on Serial IO `51E8` |
| Audio | SoundWire (unsupported) | Realtek ALC256 over HDA (AppleALC layout 11) |

## What changed in the kernel plugin

The plugin never matches the real device-id: the EFI spoofs `9A49` and the plugin keys off that and the CPU model. The
only A7A0-specific hardcode was the GPU topology patched into `AppleIntelTGLGraphicsHWT`
(`kern_gen11.cpp`, `f3b/f3bb/f3bbb`): L3 banks 8, 8 EU per subslice, **12 subslices** (96 EU).

Now the subslice count is derived at load time:

| Source | DSS | Subslices patched in | EU |
|---|---|---|---|
| real PCI device-id `0xA7AB` (default) | 4 | 8 | 64 |
| any other device (default, as before) | 6 | 12 | 96 |
| boot-arg `ngreendss=N` (1–8) | N | 2N | 16N |
| boot-arg `ngreenl3=N` (1–16) | | | L3 banks (default 8) |

The log line `topology: deviceId=… DSS=… (SS=…, EU=…) L3 banks=…` shows what was applied. 64 EU for the Core 7 240H is
from Intel's spec page; the **L3 bank count for this part is unknown** — if the GPU hangs right after the accelerator
starts, try `ngreenl3=4`.

`kern_model.hpp` also lists `A7AA`–`A7AD` (cosmetic; `getBranding()` is not called).

## What is panel-specific (and what is not)

Checked against the source; the EFI already passes `-ngreenkeepefi`.

- Not used on this setup: the `785400000` pixel-clock overrides (their routes are commented out in `kern_gen11.cpp`), and the
  2560×1600 transcoder register dump (skipped by `-ngreenkeepefi`, which keeps the firmware's timings).
- Computed from the live pipe: the plane stride. Mode id `0x80001000` is "detailed timing index 0", not a panel-specific value.
- `ngreenblmax=48000` is the duty Apple's driver treats as 100 %; the hardware period is read from the PWM register, so it
  should not depend on the panel. If `xe-brilho` prints an invalid period the panel probably uses **eDP AUX backlight**,
  which this fork does not implement.
- Connector table (`getOSInformation`): connector 1 is the reference laptop's USB-C/DDI-D port. It only affects external displays.

## The EFI (`EFI/`)

Built from the upstream project's EFI plus a known-good Sequoia EFI of this laptop (the one-off merge script is in git
history, commit `fc61f09`, `tools/make-asus-efi.py`):

- **Kept from this repo:** OpenCore 1.0.8, newer kexts, the GPU stack (NootedGreen, WhateverGreen, iGPU properties,
  boot-args, SIP value), `MacBookPro16,2` (Tahoe does not support `iMacPro1,1`).
- **Taken from the Sequoia EFI:** the four SSDTs (`PLUG-ALT`, `EC-USBX-LAPTOP`, `AWAC`, `XOSI`) and the `_OSI` rename,
  the USB map (`UTBMap`, re-keyed to `MacBookPro16,2`), `AppleALC` (+ layout-id at `1f.3`), `ECEnabler`,
  `CpuTopologyRebuild` enabled, and the Booter/Kernel/UEFI quirks that booted this laptop (`DisableIoMapper`,
  `SetupVirtualMap`, empty MMIO whitelist, `Resolution=Max`, …).
- **Added:** the RTX 5050 is hidden with `class-code=FFFFFFFF` + `disable-gpu` at `PciRoot(0x0)/Pci(0x1,0x0)/Pci(0x0,0x0)`;
  RestrictEvents CPU name is `Intel(R) Core(TM) 7 240H`.
- **Removed (reference-laptop-only):** its SSDTs/patches, MMIO whitelist, `itlwm`, Intel Bluetooth kexts, `BlueToolFixup`,
  `BrightnessKeys`, `XHCI-unsupported`, sensor kexts, PS/2 mouse/trackpad plug-ins.

Serial/MLB/UUID are empty and ROM is zero in the tracked folder. Generate yours (on a Mac): `zsh make-efi.sh` →
`dist/EFI`.

> `EFI/OC/Kexts/NootedGreen.kext` is the prebuilt binary inherited from the upstream EFI and **does not contain the
> topology change above**. Rebuild it on the target (`zsh build-nootedgreen.sh`) and replace it before the first
> accelerated boot, otherwise the plugin patches in 96 EU.

## Network during the install (Android USB tethering)

The Realtek Wi-Fi has no macOS driver, so the EFI carries **HoRNDIS 9.4** ([Apheleos fork](https://github.com/Apheleos/HoRNDIS),
Intel kext, extracted from its release `.pkg`; the same tethering worked under Sequoia on this laptop) and injects it in both
configs, so a phone in *USB tethering* mode gives the macOS installer/Recovery network access. Caveats: the fork's README
lists macOS 11–15 and says Tahoe is not supported, and its `.pkg` refuses to install on macOS 26 (that only affects the
package; OpenCore injects the kext directly). If it does not load on Tahoe, a USB ethernet adapter is the fallback. After
installing, the kext is injected on every boot; no separate install is needed.

## Bring-up order

1. `zsh make-efi.sh`; copy `dist/EFI` to the EFI partition of a Tahoe installer.
2. Install with `OC/config-install.plist` renamed to `config.plist` (NootedGreen off, verbose). No Wi-Fi: use ethernet
   or a USB adapter. Keep a second bootable disk.
3. On the installed system: `zsh build-nootedgreen.sh`, copy the new kext into `EFI/OC/Kexts`, restore the accelerated
   `config.plist`, `zsh install.sh`, reboot.
4. Send/inspect `log show | grep ngreen`: the `topology:` line, then the first display and accelerator messages.

## Known risks, in the order I would check them

1. `CpuTopologyRebuild` is enabled (it was needed on Sequoia for the 6P+4E layout). On the reference laptop it stopped the Tahoe boot
   before the progress bar (with E-cores off); if this laptop does the same, set it to `Enabled=false` in `config.plist`.
2. L3 bank count (`ngreenl3`).
3. Backlight type (PWM vs AUX) and the external connector table.
4. Anything in the display path that was only exercised with the reference laptop's 4-lane HBR3 panel (this one is smaller and may be
   2-lane).

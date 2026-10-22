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

The Realtek Wi-Fi has no macOS driver, so the EFI carries the **original HoRNDIS 9.2** ([jwise/HoRNDIS](https://github.com/jwise/horndis)
release `rel9.2`, 2018, extracted from its `.pkg`; the same tethering worked under Sequoia on this laptop) and injects it in
both configs, so a phone in *USB tethering* mode gives the macOS installer/Recovery network access. It is a thin x86_64 kext
that uses `IOUSBHostFamily`. Its last release predates Tahoe and nobody has reported it on macOS 26, so whether it loads
there is **untested**; a USB ethernet adapter is the fallback. After installing, the kext is injected from the EFI on every
boot; no separate install is needed. Check with `kextstat | grep -i horndis` and `networksetup -listallhardwareports`.

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

## Bring-up notes (A7AB, 22 Oct 2026)

- **L3 banks.** `MIRROR_FUSE3 (0x9118)` reads `0x88` on the A7AB. i915 computes `l3bank_mask = ~fuse3 & 0xF` (two banks per bit), so
  this part has **6** L3 banks, not 8. `gpuTopology()` defaults to 6 for 0xA7AB (`ngreenl3=N` overrides). With 8 the screen was garbled.
  `ngreenstatedump=5` logs the fuse registers (`fuses:` lines), including `L3ALLOC` (`0xb134`, reads `0xd0000020` at reset).
- **Start the accelerator natively** (personality `IOPCIPrimaryMatch 0x9a498086`, as `macos/install-macos.sh` does). Loading it late with a
  LaunchDaemon leaves `IGAccelDisplayMachine` without display pipes and panics (NULL pointer in
  `IGAccelDisplayPipe::allocateScanoutMemory`) on the next display-mode set. `tools/xe-native.sh` switches the AuxKC to the native match.
- **Firefox / multi-process apps.** With `amfi_get_out_of_my_way=1` the kernel kills child processes that send their task port
  (`EXC_GUARD type=1 flavor=0x20`, `plugin-container`), so Firefox opens a window and never loads a page. Boot-arg
  `ipc_control_port_options=0` turns the control-port hardening off and fixes it.

## Audio (Realtek ALC256 over HDA)

macOS Tahoe no longer ships AppleHDA, so AppleALC alone has nothing to patch (the SystemKC only has AppleGFXHDA for HDMI). Two things are needed:

1. The PCH audio controller (HDEF, `8086:51ca`, `PciRoot(0x0)/Pci(0x1f,0x3)`) reports PCI class `0x040100` (DSP mode). AppleHDAController only
   matches `0x0403`, so `EFI/OC/config.plist` sets the device property `class-code = 00 03 04 00`.
2. A native AppleHDA. [SimpleHDA](https://github.com/laobamac/SimpleHDA) (a Lilu plugin) restores it through OpenCore injection, with AppleALC
   (`alcid=11`) and `keepsyms=1`. Its kext embeds Apple's AppleHDA binaries, so it is **not committed here**:
   `zsh tools/install-simplehda.sh /Volumes/BOOT/EFI/OC [config.plist ...]` downloads the pinned release, verifies its SHA-256, copies it to
   `EFI/OC/Kexts` and adds the `Kernel -> Add` entry after AppleALC. `-shdaoff` disables it.

Result: Built-in Output and Built-in Microphone appear and sound plays. The XPS 13 Plus audio code from upstream (AudShim, SOF/SoundWire) does not apply to this laptop.


## OpenGL (accelerated, through Metal)

OpenGL on this GPU used to be Apple's software renderer only. Two things were wrong, and the second has a workaround:

1. **Where the driver is looked up.** OpenGL.framework opens `/Library/GPUBundles/<IOGLBundleName>.bundle`, then
   `/System/Library/Extensions/...`; not `/Library/Extensions`. Found with
   `sudo fs_usage -w -f filesys <program>`. The Metal and shared driver bundles are in `/Library/GPUBundles` too.
2. **Apple's Intel TGL OpenGL driver cannot be used.** Its shader compiler (`USC::CreateCompiler`, `GLRenderer/tgl/usc_interface.cpp`)
   switches on the PCI device id and knows only ids up to Ice Lake (0x8a50-0x8a71), so 0x9a49 hits an assert in
   `CGLCreateContext`. Faking an Ice Lake id would give Gen11 code for Gen12 hardware.

What works is Apple's own OpenGL-on-Metal layer, `/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle` (it has an
x86_64 slice). The framework builds the executable path from the driver *name* (`<bundle>/Contents/MacOS/<name>`) and ignores
`CFBundleExecutable`, so `tools/install-metal-gl.sh` creates `/Library/GPUBundles/AppleIntelTGLGraphicsGLDriver.bundle` as a
shell of symlinks whose executable name matches and which points at the Metal renderer. `tools/uninstall-metal-gl.sh` puts
the original bundle back. Result (`tools/diagnostics/gpu-tests/gl-probe.c`): `Intel(R) TGL Unknown`, `4.1 Metal - 90.5`,
GLSL 4.10, shaders compile and draw correct pixels. Only message: `setProvokingVertexMode: unsupported` (harmless).

Both scripts need `sudo`. A macOS update may replace or invalidate the symlinks; rerun the script.


## Java (Minecraft, Swing) crashed at the first safepoint / null check: libxedrv's crash handler

Symptom: any Java program that created a Metal device (a Swing window, Minecraft) died with `EXC_BAD_ACCESS` in JIT code
(`SIGSEGV` at 0x8/0xc, or `SIGBUS KERN_PROTECTION_FAILURE` on the safepoint polling page), `terminated by: exc handler`
(the process itself), with no `hs_err` file. HotSpot uses faults on purpose (implicit null checks, safepoint polls) through
its own `SIGSEGV`/`SIGBUS` handler. `libxedrv.dylib` (loaded by the Metal driver, after the JVM has started) replaced those
handlers with its v9 crash logger and then reset them to `SIG_DFL`. Seen with an injected watcher that polls `sigaction`
(`libjvm javaSignalHandler` -> `libxedrv crash_handler`). Fix in `macos/src/libxedrv.m` (v18): keep the previous handler of
SIGSEGV/SIGBUS/SIGILL/SIGABRT and forward the fault to it; only log and reset when there was none. The installed library was
built from v15 (commit 5b16356; v16 and v17 were never installed), so `tools/install-xedrv-fix.sh v15fix` installs v15 + only
this fix and `v17fix` installs the current source with it. `tools/restore-xedrv.sh` undoes it. New processes pick it up.


## Stale index data after a shader write (Minecraft water spikes, Firefox/Chrome upload glitches)

Symptom: Minecraft 26.3 (OpenGL 4.1 over Metal) draws long spikes and flickering in the translucent water layer, text and icons
partly missing in menus; Firefox's GPU mode smears. Found with the probes in `tools/diagnostics/gpu-tests/`:
`gl-probe-draw.c` (base vertex, indirect, vertex buffer updates: all pass), `gl-probe-formats.c` (14 vertex formats, culling,
depth, blending, flat varyings: pass; **index buffer rewritten between draws: FAIL**), `gl-probe-index-update.c`, and
`metal-index-probe.m` (the same without OpenGL):

| update path before an indexed draw from the same buffer | wrong rows of 256 |
|---|---|
| CPU write (waiting) / blit copy / blit in its own command buffer / blit + barrier | 0 |
| **compute-kernel copy into the index buffer** | **60-80** |
| compute copy + `memoryBarrierWithScope:` (render or compute encoder), or + a blit in between | 25-63 (no help) |
| compute copy into a *different* buffer for every row | 0 |
| compute copy into the same buffer, **GPU waited on after every row** | 80 |

So it is not a race: the index-fetch (VF) cache keeps old data for an address and a *shader* write to that address does not
invalidate it (CPU writes and blits do; a new address misses the cache). `MTLBarrierScopeBuffers` does nothing visible. Not fixed
by libxedrv v15/v16/v17. Candidate fixes (none tried): emit a PIPE_CONTROL with VF cache invalidate when the same index buffer
is drawn again (patch the Metal driver, as `macos/patch-mtl17.py` does for other things), or make whatever the GL layer uses
for `glBufferSubData` a blit instead of a shader copy.


### Correction and further results (same day): it is a compute -> draw ordering bug inside one command buffer

The "stale index cache" reading above was incomplete. Later runs of `metal-index-probe.m`:

- compute copy, **GPU waited on, then the draw in a separate command buffer** (M12): CPU readback and the draw are both correct (0 of 256 wrong). The compute write does reach memory and is not stale.
- compute copy and the draw in the **same command buffer** (M3, M11): 40-140 of 256 wrong, even when the GPU is waited on after each command buffer. Timing dependent (the count changes between runs).
- None of these cures it: `memoryBarrierWithScope:` in the render or compute encoder, an `MTLFence` compute -> render (worse: 170), `useResource:usage:` on the index buffer, binding the index buffer as a vertex buffer, a blit between, a different buffer per row (this one does avoid it: 0), or issuing PIPE_CONTROLs with any FLUSH_TYPE mask (all bits set included) from the render encoder before each indexed draw (`writePipeControl`, global symbol in the driver, C++ object in ivar `_pIGRenderEncoder`; test hook in the session scratchpad, not in the repo).
- Apple's GL-on-Metal layer does `glBufferSubData` as: blit into a scratch buffer, then a 1x1x1 compute kernel writing the destination, then the draw, all in one command buffer (seen with a spy library hooking `MTLIGAccel*CommandEncoder`).

So within a command buffer the driver does not stall the draw until a preceding compute dispatch has finished (the barrier code only sets dirty bits that no draw turns into a stall). Calling the base-class `IGAccelEncoderBase::writePipeControl` (local symbol, driver +0x98cb8) on the compute encoder from a hook crashed. Not fixed. Untried: a real patch of the driver's compute `endEncoding` (+0x1e4ac `IGAccelComputeCommandEncoder::endEncoding`) to emit a CS-stall PIPE_CONTROL; Minecraft's Vulkan backend (MoltenVK) does not go through the GL layer.


## Minecraft's Vulkan backend (MoltenVK) crashed in the driver's GPU counter code

`hs_err`/library fault log: SIGSEGV in `objc_retain` <- `objc_getProperty` <- `MTLIGCounterSampleBuffer::Init` <-
`MTLIGCommonCounterManager::CreateCounterSampleBuffer` <- `-[MTLIGAccelDevice newCounterSampleBufferWithDescriptor:error:]`
<- libMoltenVK (Minecraft enables "Calibrated Timestamps"). The device advertised `counterSets` (timestamp, statistic,
stageutilization) and `supportsCounterSampling:` for draw, dispatch and blit boundaries, but creating a counter sample buffer
dereferences a null member. libxedrv v19 turns counter sampling off (`supportsCounterSampling:` NO, empty `counterSets`,
`newCounterSampleBufferWithDescriptor:error:` returns nil with an error), the same way it already disables binary archives and
dynamic libraries. Verified with a Metal probe: 3 counter sets before, 0 after.

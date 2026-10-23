# Changelog

Entries dated before the ASUS port were tested on the reference laptop this GPU stack was developed on; entries about the
ASUS Vivobook V16 stay **testing** until they are confirmed on it. Status values:
**confirmed** (works on hardware), **testing** (built and installed, being evaluated), **reverted**.
The full story behind each item is in [docs/HISTORY.md](docs/HISTORY.md).

## Unreleased

| Date | Change | Status |
|---|---|---|
| 2026-10-23 | **Bluetooth (Realtek USB `13d3:3571`)**: `BTIDSpoof.kext` 1.3.0 (idProduct `0x3571`→`0x3568` so `bluetoothd` accepts it), 14-byte `bluetoothInternalControllerInfo`, `BT_HCI_TRANSPORT=USB` launch daemon, with RealtekBluetoothFirmware + BlueToolFixup. `build-btidspoof.sh`, `macos/bluetooth/`. Controller On, Bluetooth 5.2, pairing works; sleep/wake untested | confirmed |
| 2026-10-23 | **V130 `barrierSubmission` no longer a bypass** (ASUS V16): default mode 4 follows Apple's fallback path (merge pending events of every segment resource list, `writeEventBarrierCommand`, else `finishEvent`) without the BCS blit-context part that crashed. Fixes the compute -> draw ordering bug: probe modes M3/M6/M7/M8/M9/M11/M14/M15 from 10-92 wrong rows of 256 to 0, Minecraft on OpenGL almost glitch-free. Explicit `MTLFence` (M13) still wrong. `-ngreenV130pass` restores the bypass | confirmed |
| 2026-10-23 | **`libxecompfix`**: in-memory patch of `MTLCompiler.framework` (loaded into `MTLCompilerService` by our bridge plugin) so signed-integer flat varyings link; WebRender's pipelines no longer fall back to software vertex processing (`tools/build-xecompfix.sh`, `install-xecompfix.sh` with automatic rollback, `uninstall-xecompfix.sh`) | confirmed (link); does not fix Firefox on native GL |
| 2026-10-23 | **XE-RC6FW and XE-GPUREC taken from upstream** (813d2e6, source only): forcewake imbalance fix (leak seen on the A7AB: `xe-fw-underflow` 5, `xe-fw-leakdrops` 1) and sibling-engine recovery after a GPU reset (installed, `sig=11111 route=1`, not yet exercised by a real reset) | RC6FW confirmed active; GPUREC testing |
| 2026-10-23 | **Hardware video decode enabled** (`-ngreengva` + patched VA plugin): H.264 and HEVC bit-exact against software decode (`xe-vtcmp.m`, 41.5 MB compared per case), Firefox plays with hardware decoding on a normal launch (decode runs in the system `VTDecoderXPCService`). Safari stays on the CPU (sandbox) | confirmed |
| 2026-10-23 | Firefox runs on **ANGLE**; native OpenGL still garbles (opaque + alpha pass; artifacts baked into the tiles). Probes for texture uploads, large quads, depth, texture-array targets, derivatives and a GL call tracer (`tools/diagnostics/gl-trace/glspy.c`) added; investigation parked with two real-shader patches ready | ANGLE confirmed; native GL open |
| 2026-10-23 | Docs corrected: HEVC was described as "not in this tree yet" (XE-GVAHEVC has been since 2026-10-21) and the ordering bug as "not fixed"; `xe-vtdec.m` had `@('BGRA')`, which boxes a single character (pixel format 65) | confirmed |
| 2026-10-21 | **ASUS Vivobook V16 port (Core 7 240H, `8086:A7AB`, 64 EU)**: GPU topology patch (`f3b/f3bb/f3bbb`) is no longer fixed at 96 EU/12 subslices; default follows the real device-id (A7AB → 4 DSS/8 subslices) with `ngreendss=` / `ngreenl3=` overrides. New `EFI-ASUS/` (built by `tools/make-asus-efi.py`, `zsh make-efi.sh asus`) from this EFI plus a Sequoia EFI verified on the laptop. See [docs/ASUS-V16.md](docs/ASUS-V16.md) | not booted yet |
| 2026-10-21 | **XE-BLITALIGN v2**: only misaligned large copies go to the copy engine; aligned ones use the 3D blit again. Fixes the missing Chrome toolbar/page icons caused by v1 (confirmed by a boot with the patch off); screen recording and video conversions still pass | confirmed |
| 2026-10-21 | AppleGVA `hwgva-id` is now the real board-id (was iMacPro1,1 from NootedRed, which makes AppleGVA accept only AMD plugins) and XE-GVAID (IOVARendererID 0x1080080 with `-ngreengva`) | testing |
| 2026-10-21 | Hardware decode bring-up (WIP, off by default): `macos/vadriver/` patches the TGL VA plugin (device-id 9A49, VAME name, Tahoe bitstream request, unknown driver parameter, capability-node layout 0x430→0x438); install in `/Library/GPUBundles`. AppleGVA now creates the decoder up to the NV12 output surface. Tracer: `tools/diagnostics/xe-vatrace.c` | in progress |
| 2026-10-20 | **Safari video works**: it needs an audio output device, and this laptop has none on macOS (SoundWire audio unsupported). `tools/build-blackhole.sh` builds the BlackHole virtual output (GPL-3.0) without Xcode; with it (or Bluetooth headphones) YouTube plays in Safari | confirmed |
| 2026-10-20 | **XE-BLITALIGN + XE-PAGESYNC**: video surfaces from CoreVideo now work on the GPU. Screen recording is no longer green, all VTPixelTransfer conversions carry content (12/12, was 3/12), YouTube 4K in Chrome no longer flickers. Root causes: misaligned plane copies went to the broken 3D blit, and render work did not wait for the copy engine. Test tools: `tools/diagnostics/gpu-tests/` | confirmed |
| 2026-10-20 | XE-GTFREQ floor raised to 1500 MHz (RP0); the PCU currently grants ~1300 MHz | testing |
| 2026-10-20 | CpuTopologyRebuild 2.0.2 (with E-cores off) stops the boot before the progress bar; E-cores on without it boots the kernel but WindowServer hangs | reverted |
| 2026-10-20 | **Input without polling**: trackpad on a GPIO interrupt, touchscreen on an APIC interrupt; `-vi2c-force-polling` removed. Fixes the intermittent keyboard/trackpad/touchscreen freezes (I2C bus stuck while polling). Three pieces: VoodooGPIO built from current source with Tiger Lake-LP support (`build-voodoogpio.sh`) plus an upstream bug fix (`patches/voodoogpio-tigerlakelp-start.diff`); the reference laptop's I2C SSDT now returns the firmware's own resources (that SSDT was removed in the ASUS port, which uses its own) | confirmed |
| 2026-10-20 | **XE-FBDPMS**: display sleep only turns the backlight off; the pipe/transcoder/DDI path to the panel is never torn down, so the display wakes again (the TGL framebuffer cannot retrain the eDP link on display 13). Hooks `AppleIntelFramebuffer::setFramebufferPowerState`; `-ngreenfbdpms` disables | confirmed |
| 2026-10-20 | XE-NODPMS (register-write filter for the same problem): broke the framebuffer's own mode sets (spinlock timeout at boot); kept as opt-in `-ngreennodpms` only | reverted |
| 2026-10-20 | `tools/xe-sleep-diag.sh`, `tools/xe-modeset-diag.sh`: display register snapshots around display sleep / mode changes (need XeShim) | — |
| 2026-10-20 | **MTL18**: `mtl17-patches.json` no longer rewrites `0x7ff77`/`0x80253`. The v11 L3 patch had turned two `movb` instructions in `IntelState::IntelState()` into `rolb`/`rorb` | testing |
| 2026-10-20 | `make-efi.sh` (EFI with your own SMBIOS) and `install.sh` (check, fetch, build, install, uninstall) | confirmed (build path) |
| 2026-10-20 | Deep documentation: HISTORY, EXPERIMENTS, CATALOG | — |

## v0.1 — 2026-10-20 (first public snapshot)

| Area | Change | Status |
|---|---|---|
| Kernel | XE-GTFREQ: GPU frequency floor (1100 MHz) — the GPU was stuck at 100 MHz | confirmed |
| Kernel | PLANES2: IOSurface plane count read at `+0x98` (Tahoe) → HEIC/YUV no longer green | confirmed |
| Kernel | XE-NOGVA: hide hardware video decode → VideoToolbox uses software decode | confirmed (Chrome) |
| Kernel | XE-NOORPHAN, XE-RESFREE, XE-NODEPTH: memory/resolve panics | confirmed |
| Metal | libxedrv v15: copy zero-copy textures at unaligned offsets → app icons and image layers visible | confirmed |
| Repo | reproducible MTL17 patcher and shader-compiler build; EFI without serials | confirmed |

See [docs/STATUS.md](docs/STATUS.md) for what works and the known problems.

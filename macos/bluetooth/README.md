# Bluetooth on the ASUS V16 (Realtek USB controller `13d3:3571`)

Works (controller On, real address, Bluetooth 5.2, BLE discovery, pairing with an audio device; confirmed on macOS 26.7.1).
Sleep/wake and other profiles are untested.

## Why it needs more than a firmware kext
`bluetoothd` (x86_64 slice) picks its transport from the board id of the Mac it thinks it is (here a UART model) and only accepts
a USB controller whose vendor/product id is on a short allow-list (`Found_USB_Dongle`). Found by disassembling `/usr/sbin/bluetoothd`.
Four pieces together make it use the USB controller:

| Piece | What it does |
|---|---|
| `RealtekBluetoothFirmware.kext` 1.0.2 + `BlueToolFixup.kext` 2.7.2 (not in this repo) | firmware load; BlueToolFixup lets `bluetoothd` accept a third-party controller |
| `BTIDSpoof.kext` 1.3.0 (`src/`, build with `zsh build-btidspoof.sh`) | matches `13d3:3571` on `IOUSBHostDevice` and sets `idProduct` to `0x3568` once (32-bit number), which is on the allow-list. Nothing else is changed. Logs `BTIDSpoof: vendor=0x13d3 idProduct 0x3571 -> 0x3568` |
| NVRAM `bluetoothInternalControllerInfo` = **14 zero bytes**, `bluetoothExternalDongleFailed` = 1 zero byte | the daemon rejects any other length (28 bytes was rejected). Use the OpenCore `NVRAM > Add` for GUID `7C436110-AB2A-4BBB-A880-FE41995C9F82` and put both keys in `NVRAM > Delete` too so a stale value is replaced |
| `BT_HCI_TRANSPORT=USB` (`install-bt-transport.sh`, a launch daemon doing `launchctl setenv`) | selects the USB transport at every boot; undo with `uninstall-bt-transport.sh` |

`BTIDSpoof.kext` goes into `EFI/OC/Kexts`, with a `Kernel > Add` entry: `BundlePath BTIDSpoof.kext`, `ExecutablePath Contents/MacOS/BTIDSpoof`,
`Arch x86_64`. The tracked `EFI/` does not include it (the keys above are per-machine setup).

## What did not work (and why not to repeat it)
- Spoofing only the id (v1.1): `bluetoothd` found the device but kept looking for the internal controller.
- Also renaming `USB Product Name` to `Bluetooth USB Host Controller` (v1.2): makes `Found_USB_Dongle` skip the device as an *internal* one.
  Leave the name alone (`Bluetooth Radio`).
- The 28-byte NVRAM default: logged as rejected, then the default chipset BCM_4350C2 was chosen.

## Reading the logs
`STATUS 408` / `Invalid chipset` lines are **not** proof of failure. Look for `PoweredOn`, a nonzero hardware address and the
service capabilities in `CBStackController` / `GetControllerInfo`. `system_profiler SPBluetoothDataType` can still print no controller.

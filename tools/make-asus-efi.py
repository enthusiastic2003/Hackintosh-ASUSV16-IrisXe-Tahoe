#!/usr/bin/env python3
"""Builds an EFI for the ASUS Vivobook V16 (V3607VH, Core 7 240H, Iris Xe A7AB) from this repository's Dell EFI plus a
known-good Sequoia EFI of the same machine.

  python -I tools/make-asus-efi.py <sequoia-EFI-dir> [--out dist/EFI-ASUS] [--serial S --mlb M --uuid U --rom HEX12]
  python -I tools/make-asus-efi.py <sequoia-EFI-dir> --out EFI-ASUS --blank      # regenerates the tracked folder

Keeps the repository's OpenCore, newer kexts, GPU stack (NootedGreen, WhateverGreen, iGPU properties, boot-args) and
MacBookPro16,2 (Tahoe does not support iMacPro1,1). Takes the machine-specific parts from the Sequoia EFI: ACPI SSDTs
and patch, USB map (UTBMap), AppleALC/ECEnabler, and the Booter/Kernel/UEFI quirks verified to boot this laptop.
Drops the Dell-only pieces (SSDTs, MMIO whitelist, itlwm, Intel Bluetooth, XHCI-unsupported, sensor kexts).
The output holds SMBIOS data: keep it out of git (dist/ is ignored).
"""
import argparse, plistlib, shutil, sys, uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DROP_KEXTS = ["BlueToolFixup", "BrightnessKeys", "IntelBTPatcher", "IntelBluetoothFirmware", "itlwm", "XHCI-unsupported",
              "SMCDellSensors", "SMCLightSensor", "SMCSuperIO", "UTBDefault"]
DROP_PS2 = ["VoodooPS2Mouse", "VoodooPS2Trackpad"]          # the Sequoia EFI loads the keyboard plug-in only
SEQ_KEXTS = ["ECEnabler", "AppleALC", "UTBMap"]              # not in the Dell EFI
TARGET_MODEL = "MacBookPro16,2"


def kext_entry(name, executable=True):
    return {"Arch": "Any", "BundlePath": f"{name}.kext", "Comment": "", "Enabled": True,
            "ExecutablePath": f"Contents/MacOS/{name}" if executable else "",
            "MaxKernel": "", "MinKernel": "", "PlistPath": "Contents/Info.plist"}


def merge_config(repo_cfg, seq_cfg, install):
    c = repo_cfg

    # ACPI: the Sequoia set replaces the Dell one wholesale (SSDTs are tied to the machine's DSDT)
    c["ACPI"]["Add"] = seq_cfg["ACPI"]["Add"]
    c["ACPI"]["Patch"] = seq_cfg["ACPI"]["Patch"]
    c["ACPI"]["Delete"] = []

    # Booter
    c["Booter"]["MmioWhitelist"] = []
    c["Booter"]["Quirks"]["SetupVirtualMap"] = seq_cfg["Booter"]["Quirks"]["SetupVirtualMap"]
    c["Booter"]["Quirks"]["ResizeAppleGpuBars"] = seq_cfg["Booter"]["Quirks"]["ResizeAppleGpuBars"]

    # Kernel quirks
    for q in ("DisableIoMapper", "CustomSMBIOSGuid"):
        c["Kernel"]["Quirks"][q] = seq_cfg["Kernel"]["Quirks"][q]

    # Kernel.Add
    add = [k for k in c["Kernel"]["Add"]
           if k["BundlePath"].rsplit("/", 1)[-1][:-len(".kext")] not in DROP_KEXTS + DROP_PS2]
    names = [k["BundlePath"] for k in add]
    for k in add:
        if k["BundlePath"] == "CpuTopologyRebuild.kext":
            k["Enabled"] = seq_cfg_kext(seq_cfg, "CpuTopologyRebuild.kext")["Enabled"]
    after = max(names.index(n) for n in ("VirtualSMC.kext", "SMCProcessor.kext", "SMCBatteryManager.kext")) + 1
    add[after:after] = [kext_entry("ECEnabler"), kext_entry("AppleALC")]
    usb = [k["BundlePath"] for k in add].index("USBToolBox.kext") + 1
    add.insert(usb, kext_entry("UTBMap", executable=False))
    c["Kernel"]["Add"] = add

    # UEFI
    c["UEFI"]["Output"]["Resolution"] = seq_cfg["UEFI"]["Output"]["Resolution"]
    for q in ("ReleaseUsbOwnership", "EnableVectorAcceleration"):
        c["UEFI"]["Quirks"][q] = seq_cfg["UEFI"]["Quirks"][q]

    # Devices: audio layout from the Sequoia EFI (iGPU entry and dGPU disable stay as in the repo)
    c["DeviceProperties"]["Add"]["PciRoot(0x0)/Pci(0x1f,0x3)"] = seq_cfg["DeviceProperties"]["Add"]["PciRoot(0x0)/Pci(0x1f,0x3)"]

    # The RTX 5050 sits behind PEG1 (00:01.0 -> 01:00.0); macOS has no driver for it, so hide it (class-code FFFFFFFF)
    c["DeviceProperties"]["Add"]["PciRoot(0x0)/Pci(0x1,0x0)/Pci(0x0,0x0)"] = {
        "class-code": bytes.fromhex("ffffffff"), "disable-gpu": bytes.fromhex("01000000")}

    # NVRAM
    guid ="7C436110-AB2A-4BBB-A880-FE41995C9F82"
    nv = c["NVRAM"]["Add"][guid]
    for k in ("bluetoothExternalDongleFailed", "bluetoothInternalControllerInfo"):
        nv.pop(k, None)
    if "alcid=" not in nv["boot-args"]:
        nv["boot-args"] += " alcid=11"
    rs = c["NVRAM"]["Add"].get("4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102", {})
    if "revcpuname" in rs:
        rs["revcpuname"] = "Intel(R) Core(TM) 7 240H"

    # SMBIOS
    c["PlatformInfo"]["Generic"]["SystemProductName"] = TARGET_MODEL
    c["PlatformInfo"]["UpdateSMBIOSMode"] = seq_cfg["PlatformInfo"]["UpdateSMBIOSMode"]
    return c


def seq_cfg_kext(seq_cfg, bundle):
    return next(k for k in seq_cfg["Kernel"]["Add"] if k["BundlePath"] == bundle)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seq_efi", type=Path, help="EFI folder copied from the Sequoia pendrive")
    ap.add_argument("--out", type=Path, default=ROOT / "dist" / "EFI-ASUS")
    ap.add_argument("--serial"); ap.add_argument("--mlb"); ap.add_argument("--uuid"); ap.add_argument("--rom")
    ap.add_argument("--blank", action="store_true",
                    help="empty serial/MLB/UUID and zero ROM, for the tracked EFI-ASUS/ folder (make-efi.sh asus fills them)")
    a = ap.parse_args()

    seq_oc = a.seq_efi / "OC"
    if not (seq_oc / "config.plist").exists():
        sys.exit(f"{seq_oc / 'config.plist'} not found")
    seq_cfg = plistlib.loads((seq_oc / "config.plist").read_bytes())

    if a.out.exists():
        shutil.rmtree(a.out)
    shutil.copytree(ROOT / "EFI", a.out)
    oc = a.out / "OC"

    # files: drop Dell-only, bring in the Sequoia ones
    for f in (oc / "ACPI").glob("*.aml"):
        f.unlink()
    for f in (seq_oc / "ACPI").glob("*.aml"):
        shutil.copy2(f, oc / "ACPI" / f.name)
    for n in DROP_KEXTS + DROP_PS2:
        for p in list((oc / "Kexts").glob(f"{n}.kext")) + list((oc / "Kexts" / "VoodooPS2Controller.kext" / "Contents" / "PlugIns").glob(f"{n}.kext")):
            shutil.rmtree(p)
    for n in SEQ_KEXTS:
        shutil.copytree(seq_oc / "Kexts" / f"{n}.kext", oc / "Kexts" / f"{n}.kext")

    # the USB map is keyed to the SMBIOS model it was generated under
    info = oc / "Kexts" / "UTBMap.kext" / "Contents" / "Info.plist"
    p = plistlib.loads(info.read_bytes())
    for pers in p["IOKitPersonalities"].values():
        if "model" in pers:
            pers["model"] = TARGET_MODEL
    info.write_bytes(plistlib.dumps(p))

    machine_uuid = a.uuid or str(uuid.uuid4()).upper()
    for name in ("config.plist", "config-install.plist"):
        cfg = plistlib.loads((oc / name).read_bytes())
        cfg = merge_config(cfg, seq_cfg, install=name == "config-install.plist")
        g = cfg["PlatformInfo"]["Generic"]
        if a.blank:
            g["SystemSerialNumber"], g["MLB"], g["SystemUUID"], g["ROM"] = "", "", "", bytes(6)
        else:
            g["SystemSerialNumber"] = a.serial or ""
            g["MLB"] = a.mlb or ""
            g["SystemUUID"] = machine_uuid
            g["ROM"] = bytes.fromhex(a.rom) if a.rom else seq_cfg["PlatformInfo"]["Generic"]["ROM"]
        (oc / name).write_bytes(plistlib.dumps(cfg))
    print(f"wrote {a.out}")
    if not a.blank and not (a.serial and a.mlb):
        print("SystemSerialNumber/MLB are empty: generate a pair for", TARGET_MODEL, "(macserial -m", TARGET_MODEL, "-g -n 1) and rerun with --serial/--mlb")


if __name__ == "__main__":
    main()

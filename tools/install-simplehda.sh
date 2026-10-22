#!/bin/zsh
# Native AppleHDA for the ASUS V16 (Realtek ALC256 over HDA) on macOS Tahoe.
#
# Tahoe no longer ships AppleHDA, so AppleALC has nothing to patch. SimpleHDA (laobamac/SimpleHDA, a Lilu plugin) restores it through
# OpenCore injection without touching the system volume. The kext embeds Apple's own AppleHDA binaries, so it is NOT kept in this
# repository: this script downloads the pinned release, checks its SHA-256, copies SimpleHDA.kext into the EFI and adds the
# Kernel -> Add entry (right after AppleALC).
#
#   zsh tools/install-simplehda.sh /Volumes/BOOT/EFI/OC [config.plist ...]   (configs default to config.plist)
#   zsh tools/install-simplehda.sh --check                                  (download + verify only)
#
# Needs: Lilu >= 1.7.1, AppleALC, boot-args `keepsyms=1` and `alcid=<layout>` (11 for this laptop), and the HDEF DeviceProperties
# `class-code` = 00 03 04 00 (the PCH audio controller reports class 0x040100; AppleHDAController only matches 0x0403).
# Boot-arg `-shdaoff` disables the driver.
set -e
VER=1.0.0
URL=https://github.com/laobamac/SimpleHDA/releases/download/v$VER/SimpleHDA-Tahoe-v$VER-RELEASE.zip
SHA=89198256664078c91155d673ee7cebb08134dad1b71d5e34b0aa4ef0d4877f8f

TMP=$(mktemp -d); trap 'rm -rf $TMP' EXIT
curl -fL --silent --show-error -o $TMP/rel.zip $URL
got=$(shasum -a 256 $TMP/rel.zip | awk '{print $1}')
[[ $got == $SHA ]] || { echo "SHA-256 mismatch for $URL"; echo "  expected $SHA"; echo "  got      $got"; exit 1; }
unzip -q $TMP/rel.zip -d $TMP/x
KEXT=$TMP/x/SimpleHDA-Tahoe-v$VER-RELEASE/SimpleHDA.kext
[[ -d $KEXT ]] || { echo "SimpleHDA.kext not found in the release"; exit 1; }
[[ $1 == --check ]] && { echo "release verified ($SHA)"; exit 0; }

OC=${1:?usage: install-simplehda.sh <EFI/OC directory> [config.plist ...]}; shift
[[ -d $OC/Kexts ]] || { echo "$OC/Kexts not found"; exit 1; }
configs=("$@"); (( $#configs )) || configs=(config.plist)

rm -rf $OC/Kexts/SimpleHDA.kext
cp -R $KEXT $OC/Kexts/SimpleHDA.kext
echo "copied $OC/Kexts/SimpleHDA.kext"

for c in $configs; do
  [[ -f $OC/$c ]] || { echo "skip $c (not found)"; continue; }
  OC_CFG=$OC/$c python3 -I - <<'PYEOF'
import os, plistlib, shutil
p = os.environ['OC_CFG']
d = plistlib.load(open(p, 'rb'))
add = d['Kernel']['Add']
if any(e.get('BundlePath') == 'SimpleHDA.kext' for e in add):
    print(os.path.basename(p), ': SimpleHDA already listed'); raise SystemExit
idx = [i for i, e in enumerate(add) if e.get('BundlePath') == 'AppleALC.kext']
if not idx:
    raise SystemExit(os.path.basename(p) + ': AppleALC.kext is not in Kernel -> Add; add it first')
shutil.copy(p, p + '.bak-pre-simplehda')
add.insert(idx[0] + 1, {'Arch': 'x86_64', 'BundlePath': 'SimpleHDA.kext', 'Comment': 'SimpleHDA 1.0.0 (native AppleHDA on Tahoe)',
                        'Enabled': True, 'ExecutablePath': 'Contents/MacOS/SimpleHDA', 'MaxKernel': '25.99.99',
                        'MinKernel': '25.0.0', 'PlistPath': 'Contents/Info.plist'})
plistlib.dump(d, open(p, 'wb'), sort_keys=False)
print(os.path.basename(p), ': SimpleHDA added after AppleALC (backup .bak-pre-simplehda)')
PYEOF
done

#!/bin/zsh
# Stage 2a (NORMAL boot only, after stage 1 works): add the Tiger Lake ACCELERATOR kext to the auxiliary kext collection.
# No GPU bundles (/Library/GPUBundles stays empty), so only the kernel side of the accelerator starts. Boot afterwards with
# NootedGreen on, -ngreentglwithgfx and ngreenstartdelay=N (config-stage2a.plist on the stick).
#   sudo zsh tools/xe-stage2a.sh
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot (kmutil ignores the auxiliary collection there)"; exit 1; }
STASH=/Library/xe-gpu-stack-off/ext; EXT=/Library/Extensions; AUX=/Library/KernelCollections/AuxiliaryKernelExtensions.kc
for k in HookCase.kext AppleIntelTGLGraphicsFramebuffer.kext; do [[ -d $EXT/$k ]] || { echo "stage 1 is not installed ($k missing)"; exit 1; }; done
[[ -d $STASH/AppleIntelTGLGraphics.kext ]] || { echo "missing stashed accelerator kext"; exit 1; }
[[ -z $(ls /Library/GPUBundles 2>/dev/null) ]] || { echo "refusing: /Library/GPUBundles is not empty (stage 2a has no GPU bundles)"; exit 1; }
for b in $EXT/*TGL*.bundle(N) $EXT/AppleIntelGraphicsShared.bundle(N); do echo "refusing: $b is installed"; exit 1; done
before=$(stat -f %m $AUX)
rm -rf $EXT/AppleIntelTGLGraphics.kext
ditto $STASH/AppleIntelTGLGraphics.kext $EXT/AppleIntelTGLGraphics.kext
chown -R root:wheel $EXT/AppleIntelTGLGraphics.kext
plutil -p $EXT/AppleIntelTGLGraphics.kext/Contents/Info.plist | grep -E 'IOPCIPrimaryMatch|"CFBundleVersion"'
/usr/bin/kmutil load -z --load-style match-only -p $EXT/HookCase.kext -p $EXT/AppleIntelTGLGraphicsFramebuffer.kext -p $EXT/AppleIntelTGLGraphics.kext 2>&1 || true
for i in {1..20}; do [[ $(stat -f %m $AUX) != $before ]] && break; sleep 1; done
python3 -I - <<'PYEOF'
import subprocess
kc=open('/Library/KernelCollections/AuxiliaryKernelExtensions.kc','rb').read()
fixed=bytes.fromhex('81b8780c0000a4381600') in kc; old=bytes.fromhex('c74138a0381600') in kc
ids=subprocess.run(['kmutil','inspect','-V','release','-B','/Library/KernelCollections/AuxiliaryKernelExtensions.kc'],capture_output=True,text=True).stdout
print('AuxKC size', len(kc), '| fixed probePortMode:', fixed, '| old probePortMode:', old)
have={b: (b in ids) for b in ('org.smichaud.HookCase','com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer','com.xxxxx.driver.AppleIntelTGLGraphics\t')}
for b,v in have.items(): print(' ', b.strip(), 'in collection:', v)
ok = fixed and not old and all(have.values())
print('STAGE 2a COLLECTION', 'OK' if ok else 'NOT AS EXPECTED - do not reboot into the stage 2a config')
PYEOF

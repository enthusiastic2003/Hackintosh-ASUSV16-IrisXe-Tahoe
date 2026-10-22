#!/bin/zsh
# Live-bring-up collection (NORMAL boot only): fixed HookCase + TGL framebuffer kext + the TGL ACCELERATOR kext with its own match
# personality made INERT (0x12348086), so nothing starts the accelerator at boot. It is started by hand later (xe-live-load.sh).
#   sudo zsh tools/xe-stage-live.sh
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot (kmutil ignores the auxiliary collection there)"; exit 1; }
ROOT=${0:A:h:h}; STASH=/Library/xe-gpu-stack-off/ext; EXT=/Library/Extensions; AUX=/Library/KernelCollections/AuxiliaryKernelExtensions.kc
[[ -d $ROOT/build/HookCase.kext && -d $STASH/AppleIntelTGLGraphicsFramebuffer.kext && -d $STASH/AppleIntelTGLGraphics.kext ]] || { echo "missing build/HookCase.kext or stashed kexts"; exit 1; }
[[ -z $(ls /Library/GPUBundles 2>/dev/null) ]] || { echo "refusing: /Library/GPUBundles is not empty"; exit 1; }
before=$(stat -f %m $AUX)
rm -rf $EXT/HookCase.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext $EXT/AppleIntelTGLGraphics.kext
ditto $ROOT/build/HookCase.kext $EXT/HookCase.kext
ditto $STASH/AppleIntelTGLGraphicsFramebuffer.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext
ditto $STASH/AppleIntelTGLGraphics.kext $EXT/AppleIntelTGLGraphics.kext
plutil -replace IOKitPersonalities.Gen7.IOPCIPrimaryMatch -string 0x12348086 $EXT/AppleIntelTGLGraphics.kext/Contents/Info.plist
chown -R root:wheel $EXT/HookCase.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext $EXT/AppleIntelTGLGraphics.kext
plutil -p $EXT/AppleIntelTGLGraphics.kext/Contents/Info.plist | grep -E 'IOPCIPrimaryMatch|"CFBundleVersion"'
/usr/bin/kmutil load -z --load-style match-only -p $EXT/HookCase.kext -p $EXT/AppleIntelTGLGraphicsFramebuffer.kext -p $EXT/AppleIntelTGLGraphics.kext 2>&1 || true
for i in {1..20}; do [[ $(stat -f %m $AUX) != $before ]] && break; sleep 1; done
python3 -I - <<'PYEOF'
import subprocess
p='/Library/KernelCollections/AuxiliaryKernelExtensions.kc'
kc=open(p,'rb').read()
fixed=bytes.fromhex('81b8780c0000a4381600') in kc; old=bytes.fromhex('c74138a0381600') in kc
ids=subprocess.run(['kmutil','inspect','-V','release','-B',p],capture_output=True,text=True).stdout
have={b:(b in ids) for b in ('org.smichaud.HookCase','com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer','com.xxxxx.driver.AppleIntelTGLGraphics\t')}
neutral = b'0x12348086' in kc
print('AuxKC size',len(kc),'| fixed probePortMode:',fixed,'| old:',old,'| inert accelerator personality (0x12348086) present:',neutral)
for b,v in have.items(): print(' ',b.strip(),'in collection:',v)
ok = fixed and not old and neutral and all(have.values())
print('LIVE COLLECTION','OK' if ok else 'NOT AS EXPECTED - do not reboot into the live config')
PYEOF

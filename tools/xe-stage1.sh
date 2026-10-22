#!/bin/zsh
# Stage 1 (NORMAL boot only): register the FIXED HookCase + the Tiger Lake FRAMEBUFFER kext in the auxiliary kext collection.
# No accelerator kext, no GPU bundles. Boot it afterwards with NootedGreen on and -ngreentglfb (framebuffer tier only).
#   sudo zsh tools/xe-stage1.sh
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot (kmutil ignores the auxiliary collection there)"; exit 1; }
ROOT=${0:A:h:h}; STASH=/Library/xe-gpu-stack-off/ext; EXT=/Library/Extensions; AUX=/Library/KernelCollections/AuxiliaryKernelExtensions.kc
[[ -d $ROOT/build/HookCase.kext && -d $STASH/AppleIntelTGLGraphicsFramebuffer.kext ]] || { echo "missing build/HookCase.kext or the stashed framebuffer kext"; exit 1; }
for x in AppleIntelTGLGraphics.kext AppleIntelGraphicsShared.bundle; do [[ -e $EXT/$x ]] && { echo "refusing: $x is installed (stage 1 has no accelerator)"; exit 1; }; done
before=$(stat -f %m $AUX)
rm -rf $EXT/HookCase.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext
ditto $ROOT/build/HookCase.kext $EXT/HookCase.kext
ditto $STASH/AppleIntelTGLGraphicsFramebuffer.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext
chown -R root:wheel $EXT/HookCase.kext $EXT/AppleIntelTGLGraphicsFramebuffer.kext
/usr/bin/kmutil load -z --load-style match-only -p $EXT/HookCase.kext -p $EXT/AppleIntelTGLGraphicsFramebuffer.kext 2>&1 || true
for i in {1..20}; do [[ $(stat -f %m $AUX) != $before ]] && break; sleep 1; done
python3 -I - <<'PYEOF'
import subprocess
kc=open('/Library/KernelCollections/AuxiliaryKernelExtensions.kc','rb').read()
fixed=bytes.fromhex('81b8780c0000a4381600') in kc; old=bytes.fromhex('c74138a0381600') in kc
ids=subprocess.run(['kmutil','inspect','-V','release','-B','/Library/KernelCollections/AuxiliaryKernelExtensions.kc'],capture_output=True,text=True).stdout
print('AuxKC size', len(kc), '| fixed probePortMode:', fixed, '| old probePortMode:', old)
for b in ('org.smichaud.HookCase','com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer','com.xxxxx.driver.AppleIntelTGLGraphics\t'):
    print(' ', b.strip(), 'in collection:', b in ids)
ok = fixed and not old and 'AppleIntelTGLGraphicsFramebuffer' in ids and 'com.xxxxx.driver.AppleIntelTGLGraphics\t' not in ids
print('STAGE 1 COLLECTION', 'OK' if ok else 'NOT AS EXPECTED - do not reboot into the accelerated config')
PYEOF

#!/bin/zsh
# Reference-style start (NORMAL boot only): the accelerator personality matches the spoofed device-id (0x9a498086) again, so the
# kernel starts the accelerator together with the framebuffer at boot (like install-macos.sh does). The late-load daemon is
# moved aside (undo: sudo zsh tools/xe-native.sh --undo).
#   sudo zsh tools/xe-native.sh
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot (kmutil ignores the auxiliary collection there)"; exit 1; }
EXT=/Library/Extensions; K=$EXT/AppleIntelTGLGraphics.kext; AUX=/Library/KernelCollections/AuxiliaryKernelExtensions.kc
PL=/Library/LaunchDaemons/local.xe.gpuload.plist; OFF=/Library/xe-gpuload-off
if [[ $1 == --undo ]]; then MATCH=0x12348086; else MATCH=0x9a498086; fi
[[ -d $K ]] || { echo "missing $K"; exit 1; }
before=$(stat -f %m $AUX)
plutil -replace IOKitPersonalities.Gen7.IOPCIPrimaryMatch -string $MATCH $K/Contents/Info.plist
chown -R root:wheel $K
if [[ $1 == --undo ]]; then
  [[ -f $OFF/local.xe.gpuload.plist ]] && { mv $OFF/local.xe.gpuload.plist $PL; launchctl bootstrap system $PL 2>/dev/null || true; }
else
  mkdir -p $OFF
  if [[ -f $PL ]]; then launchctl bootout system $PL 2>/dev/null || true; mv $PL $OFF/; fi
fi
/usr/bin/kmutil load -z --load-style match-only -p $EXT/HookCase.kext -p $EXT/AppleIntelTGLGraphicsFramebuffer.kext -p $K 2>&1 || true
for i in {1..20}; do [[ $(stat -f %m $AUX) != $before ]] && break; sleep 1; done
MATCH=$MATCH python3 -I - <<'PYEOF'
import os
kc=open('/Library/KernelCollections/AuxiliaryKernelExtensions.kc','rb').read()
want=os.environ['MATCH'].encode(); other=(b'0x12348086' if want==b'0x9a498086' else b'0x9a498086')
fixed=bytes.fromhex('81b8780c0000a4381600') in kc; old=bytes.fromhex('c74138a0381600') in kc
has=want in kc; hasother=other in kc
print('AuxKC size',len(kc),'| fixed probePortMode:',fixed,'| old:',old,'|',want.decode(),'present:',has,'|',other.decode(),'present:',hasother)
print('COLLECTION','OK' if fixed and not old and has and not hasother else 'NOT AS EXPECTED - do not reboot into the GPU config')
PYEOF

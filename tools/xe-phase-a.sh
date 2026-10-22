#!/bin/zsh
# Phase A (SAFE MODE only): put the machine back into its pre-install state so a NORMAL boot reaches a desktop again.
#   sudo zsh tools/xe-phase-a.sh
# Moves (never deletes) the Tiger Lake stack out of /Library/Extensions + /Library/GPUBundles into /Library/xe-gpu-stack-off
# (restore with: sudo zsh tools/xe-gpu-stack.sh on) and the old auxiliary kext collection into /Library/xe-disabled.
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 1 ]] || { echo "refusing: this is not a safe-mode boot"; exit 1; }
STASH=/Library/xe-gpu-stack-off; OFF=/Library/xe-disabled
EXT=/Library/Extensions; GPU=/Library/GPUBundles
EXTITEMS=(AppleIntelTGLGraphics.kext AppleIntelTGLGraphicsFramebuffer.kext HookCase.kext AppleIntelTGLGraphicsMTLDriver.bundle
          AppleIntelTGLGraphicsGLDriver.bundle AppleIntelTGLGraphicsVADriver.bundle AppleIntelTGLGraphicsVAME.bundle
          AppleIntelGraphicsShared.bundle)
GPUITEMS=(AppleIntelTGLGraphicsMTLDriver.bundle AppleIntelGraphicsShared.bundle)
mkdir -p $STASH/ext $STASH/gpu $OFF
for i in $EXTITEMS; do [[ -d $EXT/$i ]] && { rm -rf $STASH/ext/$i; mv $EXT/$i $STASH/ext/$i; echo "moved  /Library/Extensions/$i"; }; done
for i in $GPUITEMS; do [[ -d $GPU/$i ]] && { rm -rf $STASH/gpu/$i; mv $GPU/$i $STASH/gpu/$i; echo "moved  /Library/GPUBundles/$i"; }; done
AUX=/Library/KernelCollections/AuxiliaryKernelExtensions.kc
[[ -f $AUX ]] && { mv $AUX $OFF/AuxiliaryKernelExtensions.kc.old; echo "moved  $AUX -> $OFF/"; }
echo "--- what is left in /Library/Extensions:"; ls $EXT
echo "--- /Library/GPUBundles:"; ls $GPU 2>/dev/null || true
echo "--- /Library/KernelCollections:"; ls -la /Library/KernelCollections
echo "Phase A done. Next: put the NORMAL test config on the stick, then boot normally."

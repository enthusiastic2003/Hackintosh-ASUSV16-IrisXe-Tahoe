#!/bin/zsh
# Switch the installed Tiger Lake driver stack off or on without losing it.
#   sudo zsh tools/xe-gpu-stack.sh status
#   sudo zsh tools/xe-gpu-stack.sh off    move the TGL kexts/bundles out of /Library/{Extensions,GPUBundles}, rebuild the AuxKC
#   sudo zsh tools/xe-gpu-stack.sh on     put them back and register them again (same as install-macos.sh's last step)
# Why: the installed AppleIntelTGLGraphicsFramebuffer personality matches the spoofed device-id 9A49 (IOProbeScore 80000), so it
# loads on EVERY normal boot, with or without NootedGreen. Without NootedGreen's patches it runs unpatched on Raptor Lake display
# hardware. Safe mode (-x) does not load the AuxKC, so "off" can be run from a safe-mode boot to recover.
# Reboot afterwards. Nothing is deleted: the stack lives in /Library/xe-gpu-stack-off while off.
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
STASH=/Library/xe-gpu-stack-off
EXT=/Library/Extensions; GPU=/Library/GPUBundles
EXTITEMS=(AppleIntelTGLGraphics.kext AppleIntelTGLGraphicsFramebuffer.kext HookCase.kext AppleIntelTGLGraphicsMTLDriver.bundle
          AppleIntelTGLGraphicsGLDriver.bundle AppleIntelTGLGraphicsVADriver.bundle AppleIntelTGLGraphicsVAME.bundle
          AppleIntelGraphicsShared.bundle)
GPUITEMS=(AppleIntelTGLGraphicsMTLDriver.bundle AppleIntelGraphicsShared.bundle)
case ${1:-status} in
  status)
    for i in $EXTITEMS; do [[ -d $EXT/$i ]] && echo "ON   $EXT/$i" || { [[ -d $STASH/ext/$i ]] && echo "off  $i (stashed)" || echo "none $i"; }; done
    for i in $GPUITEMS; do [[ -d $GPU/$i ]] && echo "ON   $GPU/$i" || { [[ -d $STASH/gpu/$i ]] && echo "off  $i (stashed)" || echo "none $i"; }; done ;;
  off)
    mkdir -p $STASH/ext $STASH/gpu
    for i in $EXTITEMS; do [[ -d $EXT/$i ]] && { rm -rf $STASH/ext/$i; mv $EXT/$i $STASH/ext/$i; echo "stashed $i"; }; done
    for i in $GPUITEMS; do [[ -d $GPU/$i ]] && { rm -rf $STASH/gpu/$i; mv $GPU/$i $STASH/gpu/$i; echo "stashed GPUBundles/$i"; }; done
    kmutil rebuild 2>&1 || true
    echo "off. Reboot; the TGL framebuffer/accelerator no longer load." ;;
  on)
    [[ -d $STASH/ext ]] || { echo "nothing stashed"; exit 1; }
    for i in $EXTITEMS; do [[ -d $STASH/ext/$i ]] && { rm -rf $EXT/$i; mv $STASH/ext/$i $EXT/$i; echo "restored $i"; }; done
    for i in $GPUITEMS; do [[ -d $STASH/gpu/$i ]] && { rm -rf $GPU/$i; mv $STASH/gpu/$i $GPU/$i; echo "restored GPUBundles/$i"; }; done
    p=(-p $EXT/HookCase.kext -p $EXT/AppleIntelTGLGraphics.kext -p $EXT/AppleIntelTGLGraphicsFramebuffer.kext)
    /usr/bin/kmutil load -z --load-style match-only $p 2>&1 || true
    echo "on. Reboot with the accelerated config (NootedGreen enabled)." ;;
  *) echo "usage: $0 status|off|on"; exit 1 ;;
esac

#!/bin/zsh
# xe-gpuload: boot-time loader for the Tiger Lake accelerator (LaunchDaemon local.xe.gpuload, root).
# The accelerator kext sits in the auxiliary kext collection with its own match personality made inert (tools/xe-stage-live.sh), so
# IOKit does not start it by itself. Once the patched framebuffer is up this starts it by hand (kmutil load --load-style start-only);
# NootedGreen reacts to the load, patches the kext and injects its personality, which starts the accelerator.
# Guards: safe boot, a boot without NootedGreen's accelerator tier (-ngreentglhw) or without NootedGreen loaded, or an accelerator
# that is already registered all end here without loading anything (the accelerator must never run unpatched).
# Log: /Users/Shared/xe-live/gpuload.log.   XE_DRYRUN=1 prints what it would do.
LOGDIR=/Users/Shared/xe-live; mkdir -p $LOGDIR; LOG=$LOGDIR/gpuload.log
exec >>$LOG 2>&1
say() { print -r -- "$(date '+%H:%M:%S') $*"; }
boot=$(sysctl -n kern.boottime | sed -n 's/^{ sec = \([0-9]*\),.*/\1/p')
say "== xe-gpuload start (uptime $(( $(date +%s) - boot )) s)"
[[ $(sysctl -n kern.safeboot) == 1 ]] && { say "safe boot: nothing to do"; exit 0; }
[[ " $(sysctl -n kern.bootargs) " == *" -ngreentglhw "* ]] || { say "boot-args lack -ngreentglhw: NOT loading the accelerator (it would run unpatched)"; exit 0; }
wait_for() { local what=$1 tries=$2 i; shift 2; for ((i = 0; i < tries; i++)); do eval "$*" && return 0; sleep 1; done; say "timeout waiting for $what"; return 1 }
wait_for "NootedGreen" 90 "kmutil showloaded --list-only 2>/dev/null | grep -q com.StezzaPilot.NootedGreen" || exit 1
wait_for "TGL framebuffer kext" 150 "kmutil showloaded --list-only 2>/dev/null | grep -q 'com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer'" || exit 1
wait_for "framebuffer controller" 150 "ioreg -r -c AppleIntelFramebufferController 2>/dev/null | grep -q 'registered, matched, active'" || exit 1
say "framebuffer is up"; sleep 5
if ioreg -r -c IntelAccelerator 2>/dev/null | grep -q 'registered, matched, active'; then say "accelerator already registered: nothing to do"; exit 0; fi
[[ -n $XE_DRYRUN ]] && { say "dry run: would load com.xxxxx.driver.AppleIntelTGLGraphics now"; exit 0; }
say "loading the accelerator (kmutil load --load-style start-only)"
/usr/bin/kmutil load -z -b com.xxxxx.driver.AppleIntelTGLGraphics --load-style start-only
say "kmutil rc=$?"
if wait_for "accelerator registration" 180 "ioreg -r -c IntelAccelerator 2>/dev/null | grep -q 'registered, matched, active'"; then
  say "accelerator registered"
else
  say "accelerator did NOT register (see the kernel log: dmesg | grep ngreen)"
fi

#!/bin/zsh
# Stage 2b (live, NORMAL boot, accelerator already started): put the user-space Metal/GL/VA bundles back from the stash.
#   sudo zsh tools/xe-stage2b-bundles.sh
# Needs the live logger (tools/xe-live-log.py) running. Does NOT restart WindowServer; test with build/xe-metal-devices first.
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot"; exit 1; }
pgrep -f xe-live-log.py >/dev/null || { echo "start the live logger first: sudo python3 tools/xe-live-log.py"; exit 1; }
ioreg -l -w0 -r -c IntelAccelerator | grep -q IntelAccelerator || { echo "refusing: the IntelAccelerator is not running (nothing to drive)"; exit 1; }
S=/Library/xe-gpu-stack-off
for b in AppleIntelTGLGraphicsMTLDriver AppleIntelTGLGraphicsGLDriver AppleIntelTGLGraphicsVADriver AppleIntelTGLGraphicsVAME AppleIntelGraphicsShared; do
  [[ -d $S/ext/$b.bundle ]] || { echo "missing stashed $b.bundle"; exit 1; }
done
[[ -d /System/Library/Caches/xe.bundle ]] || { echo "missing /System/Library/Caches/xe.bundle (shader compiler)"; exit 1; }
put() { rm -rf "$2"; mkdir -p "$(dirname "$2")"; ditto "$1" "$2"; chown -R root:wheel "$2"; echo "  $2"; }
for b in AppleIntelTGLGraphicsMTLDriver AppleIntelTGLGraphicsGLDriver AppleIntelTGLGraphicsVADriver AppleIntelTGLGraphicsVAME AppleIntelGraphicsShared; do
  put $S/ext/$b.bundle /Library/Extensions/$b.bundle
done
put $S/gpu/AppleIntelTGLGraphicsMTLDriver.bundle /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle
put $S/gpu/AppleIntelGraphicsShared.bundle /Library/GPUBundles/AppleIntelGraphicsShared.bundle
echo "bundles installed. Now run, one at a time:  build/xe-metal-devices list | queue | submit | compute"

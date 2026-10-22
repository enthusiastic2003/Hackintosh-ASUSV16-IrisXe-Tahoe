#!/bin/sh
# Install a fixed libxedrv (crash handler that passes faults on to the previous handler, e.g. the Java VM's).
#   sudo ~/xedrv-install.sh v15fix     same code as installed now, only the handler fixed (minimal)
#   sudo ~/xedrv-install.sh v17fix     current repo version (v17: includes the Chrome UI fix), handler fixed
# Undo: sudo ~/xedrv-restore.sh
set -e
V=${1:?usage: xedrv-install.sh v15fix|v17fix}
SRC="/Users/sirjanhansda/xedrv-fix/libxedrv-$V.dylib"
D=/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/libxedrv.dylib
[ -f "$SRC" ] || { echo "no such build: $SRC"; exit 1; }
[ -f "$D.orig-v15" ] || cp -p "$D" "$D.orig-v15"          # keep the original once
cp "$SRC" "$D.new" && chown root:wheel "$D.new" && chmod 755 "$D.new"
mv "$D.new" "$D"                                          # atomic: running processes keep the old file
echo "installed $V. New processes use it; apps already running keep the old one until restarted."

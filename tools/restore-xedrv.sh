#!/bin/sh
# Put the original libxedrv back.
D=/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/libxedrv.dylib
[ -f "$D.orig-v15" ] || { echo "no backup ($D.orig-v15 missing)"; exit 1; }
cp "$D.orig-v15" "$D.new" && chown root:wheel "$D.new" && chmod 755 "$D.new" && mv "$D.new" "$D"
echo "restored the original libxedrv"

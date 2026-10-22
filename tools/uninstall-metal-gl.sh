#!/bin/sh
# Put the original Intel GL driver bundle back.
G=/Library/GPUBundles/AppleIntelTGLGraphicsGLDriver.bundle
if [ ! -d "$G.off" ]; then echo "nothing to restore ($G.off is missing)"; exit 1; fi
rm -rf "$G"        # only symlinks in there: the originals are untouched
mv "$G.off" "$G"
echo "restored the original driver bundle"

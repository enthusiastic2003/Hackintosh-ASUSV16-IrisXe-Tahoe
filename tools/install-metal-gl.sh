#!/bin/sh
# Try Apple's OpenGL-on-Metal renderer under the Intel driver's name (reversible: ~/gl-restore.sh).
# The OpenGL framework opens <bundle>/Contents/MacOS/<driver name>, so that name must exist.
set -e
G=/Library/GPUBundles/AppleIntelTGLGraphicsGLDriver.bundle
M=/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle
if [ -e "$G.off" ]; then echo "$G.off already exists: run ~/gl-restore.sh first"; exit 1; fi
mv "$G" "$G.off"
mkdir -p "$G/Contents/MacOS"
ln -s "$M/Contents/MacOS/AppleMetalOpenGLRenderer" "$G/Contents/MacOS/AppleIntelTGLGraphicsGLDriver"
for f in Info.plist version.plist Resources PlugIns _CodeSignature; do
    [ -e "$M/Contents/$f" ] && ln -s "$M/Contents/$f" "$G/Contents/$f"
done
echo "installed. Now run: ~/glprobe"

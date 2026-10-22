#!/bin/zsh
# Builds the compiler-service patch (macos/src/libxecompfix.c) and a copy of the live bridge plugin that loads it.
# No root needed. Output: build/xecompfix/{libxecompfix.dylib,libMTLIGCCompilerPlugin.dylib}. Then: sudo tools/install-xecompfix.sh
set -e
ROOT=${0:A:h:h}; OUT=$ROOT/build/xecompfix; XE=/System/Library/Caches/xe.bundle/Contents/MacOS
mkdir -p $OUT
clang -dynamiclib -arch x86_64 -mmacosx-version-min=13.0 -O2 -Wall $ROOT/macos/src/libxecompfix.c \
      -install_name @loader_path/libxecompfix.dylib -o $OUT/libxecompfix.dylib
codesign -f -s - $OUT/libxecompfix.dylib 2>/dev/null
ORIG=$XE/libMTLIGCCompilerPlugin.dylib.orig-xecompfix; [[ -f $ORIG ]] || ORIG=$XE/libMTLIGCCompilerPlugin.dylib
python3 -I $ROOT/tools/add-load-dylib.py $ORIG $OUT/libMTLIGCCompilerPlugin.dylib @loader_path/libxecompfix.dylib
codesign -f -s - $OUT/libMTLIGCCompilerPlugin.dylib 2>/dev/null
otool -L $OUT/libMTLIGCCompilerPlugin.dylib | grep -E 'xecompfix|Old'
echo "built $OUT"

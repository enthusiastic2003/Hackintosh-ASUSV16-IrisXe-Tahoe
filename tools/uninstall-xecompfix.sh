#!/bin/zsh
# sudo tools/uninstall-xecompfix.sh : restore the original bridge plugin and remove the patcher
XE=/System/Library/Caches/xe.bundle/Contents/MacOS; ORIG=$XE/libMTLIGCCompilerPlugin.dylib.orig-xecompfix
[[ $EUID == 0 ]] || { echo "run with sudo"; exit 1; }
[[ -f $ORIG ]] || { echo "no backup at $ORIG - nothing to restore"; exit 1; }
cp -p $ORIG $XE/libMTLIGCCompilerPlugin.dylib; rm -f $XE/libxecompfix.dylib; killall MTLCompilerService 2>/dev/null
echo "restored the original plugin"

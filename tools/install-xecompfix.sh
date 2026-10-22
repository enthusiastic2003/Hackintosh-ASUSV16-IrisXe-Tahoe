#!/bin/zsh
# Installs the compiler-service patch: sudo tools/install-xecompfix.sh   (run tools/build-xecompfix.sh first, as your user)
# Replaces the bridge plugin in /System/Library/Caches/xe.bundle with the copy that loads libxecompfix.dylib, restarts
# MTLCompilerService, and runs a GL compile test. If the test fails or hangs it rolls back by itself.
ROOT=${0:A:h:h}; B=$ROOT/build/xecompfix; XE=/System/Library/Caches/xe.bundle/Contents/MacOS; U=${SUDO_USER:-$USER}
[[ $EUID == 0 ]] || { echo "run with sudo"; exit 1; }
[[ -f $B/libxecompfix.dylib && -f $B/libMTLIGCCompilerPlugin.dylib ]] || { echo "build first: zsh tools/build-xecompfix.sh"; exit 1; }
PLUG=$XE/libMTLIGCCompilerPlugin.dylib; ORIG=$PLUG.orig-xecompfix
[[ -f $ORIG ]] || cp -p $PLUG $ORIG
rollback() { cp -p $ORIG $PLUG; rm -f $XE/libxecompfix.dylib; killall MTLCompilerService 2>/dev/null; echo "ROLLED BACK to the original plugin"; }
cp $B/libxecompfix.dylib $XE/libxecompfix.dylib; cp $B/libMTLIGCCompilerPlugin.dylib $PLUG
chown root:wheel $XE/libxecompfix.dylib $PLUG; chmod 755 $XE/libxecompfix.dylib $PLUG
killall MTLCompilerService 2>/dev/null; sleep 1
T=$B/gl-probe-varyings
[[ -x $T ]] || sudo -u $U clang -w $ROOT/tools/diagnostics/gpu-tests/gl-probe-varyings.c -framework OpenGL -o $T
OUTF=$(mktemp); sudo -u $U perl -e 'alarm 90; exec @ARGV' $T > $OUTF 2>&1; rc=$?
echo "--- test output (flat int cases):"; grep -E '^(smooth float|flat int|flat ivec4|smooth vec2 \+ flat ivec4|10 varyings)' $OUTF | cut -c1-120
if [[ $rc != 0 ]] || ! grep -q '^flat ivec4 .*pixel ok' $OUTF || ! grep -q '^smooth float .*pixel ok' $OUTF; then echo "TEST FAILED (rc=$rc)"; rollback; exit 2; fi
echo "installed. link-failures should now be 0 for the flat int rows; check 'xecompfix' with: log show --last 2m --predicate 'eventMessage CONTAINS \"xecompfix\"'"

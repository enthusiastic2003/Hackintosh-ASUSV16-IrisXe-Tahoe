#!/bin/zsh
# Builds HookCase.kext (NootedGreen's modified copy, symbol provider for the Tiger Lake kexts) without Xcode.
#   zsh build-hookcase.sh [path/to/HookCase-master]   -> build/HookCase.kext
# Default source: build/deps/NootedGreen/HookCase-master (fetched by install.sh). The IOKit personality is removed
# (the kext only exports symbols; nothing should match it) and OSBundleRequired is dropped.
set -e
ROOT=${0:A:h}
SRC=${1:-$ROOT/build/deps/NootedGreen/HookCase-master/HookCase}
SDK=$ROOT/build/deps/MacKernelSDK
OUT=$ROOT/build/HookCase.kext
BID=org.smichaud.HookCase; VER=1.0; NAME=HookCase
[[ -d $SDK ]] || git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git $SDK
[[ -f $SRC/HookCase.cpp ]] || { echo "HookCase source not found in $SRC (run install.sh --build-only first)"; exit 1; }
TMP=$(mktemp -d)
# MacKernelSDK declares strnstr(char *, ...) while HookCase.h uses the newer -fbounds-safety signature: build from a copy
# with the declaration and definition aligned to the SDK's.
cp $SRC/HookCase.cpp $SRC/HookCase.h $SRC/HookCase.s $SRC/Info.plist $TMP/
sed -i '' 's/char \*__null_terminated strnstr(const char \*__null_terminated s,/char *strnstr(char *s,/' $TMP/HookCase.h $TMP/HookCase.cpp
SRC=$TMP
# NULL-callback fix: both probePortMode() bodies read SHOTPLUG_CTL (0x1638a0) through a global `callback` that is never assigned
# (HookCase::start, which would set it, is commented out and the kext has no personality) -> kernel NULL dereference as soon as
# the Tiger Lake framebuffer probes its ports. Read the register through the controller that probePortMode() already has (`bc`).
python3 -I - $TMP/HookCase.cpp <<'PYEOF'
import sys
p=sys.argv[1]; s=open(p).read()
helper = """
// xe: read a register through the framebuffer controller's own MMIO mapping (offsets as in ReadRegister32 below)
static uint32_t hcReadBcReg(void *bc, unsigned long off) {
    if (!bc) return 0xffffffffu;
    uint32_t len = getMember<uint32_t>(bc, 0xC78);
    uint64_t base = getMember<uint64_t>(bc, 0x9B0);
    if (!base || off + 4 > len) return 0xffffffffu;
    return *reinterpret_cast<volatile uint32_t *>(base + off);
}

uint32_t AppleInteePortHAL::probePortMode()"""
assert s.count("\nuint32_t AppleInteePortHAL::probePortMode()")==1
s=s.replace("\nuint32_t AppleInteePortHAL::probePortMode()", helper, 1)
old="callback->readReg32(0x1638a0)"
assert s.count(old)==2
s=s.replace(old,"hcReadBcReg(bc, 0x1638a0)")
open(p,'w').write(s)
PYEOF

rm -rf $OUT; mkdir -p $OUT/Contents/MacOS
sed -e "s/\$(PRODUCT_BUNDLE_IDENTIFIER)/$BID/g" -e "s/\$(PRODUCT_NAME:rfc1034identifier)/$NAME/g" \
    -e "s/\$(PRODUCT_NAME)/$NAME/g" -e "s/\$(EXECUTABLE_NAME)/$NAME/g" -e "s/\$(MODULE_VERSION)/$VER/g" \
    $SRC/Info.plist > $OUT/Contents/Info.plist
plutil -remove IOKitPersonalities $OUT/Contents/Info.plist
plutil -remove OSBundleRequired $OUT/Contents/Info.plist

FLAGS=(-arch x86_64 -mkernel -nostdinc -fno-builtin -fno-common -fno-stack-protector
       -mmacosx-version-min=13.0 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
       -DPRODUCT_NAME=$NAME -DMODULE_VERSION=$VER -D__null_terminated= -D__stateful_pure= -isystem $SDK/Headers -O2 -g -w)
cat > $TMP/info.c <<EOC
#include <mach/mach_types.h>
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL($BID, "$VER", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
EOC
clang $FLAGS -c $TMP/info.c -o $TMP/info.o
clang++ $FLAGS -std=gnu++17 -nostdinc++ -fno-exceptions -fno-rtti -fapple-kext -I$TMP -c $SRC/HookCase.cpp -o $TMP/hc.o
clang $FLAGS -x assembler-with-cpp -c $SRC/HookCase.s -o $TMP/hcs.o
clang++ -arch x86_64 -mkernel -nostdlib -static -Xlinker -kext -Xlinker -export_dynamic -mmacosx-version-min=13.0 \
  $TMP/*.o $SDK/Library/x86_64/libkmod.a -lcc_kext -o $OUT/Contents/MacOS/$NAME
codesign -f -s - $OUT >/dev/null 2>&1 || true
rm -rf $TMP
echo "built $OUT"

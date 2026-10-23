#!/bin/zsh
# Builds BTIDSpoof.kext (macos/bluetooth/src) with the Command Line Tools only.
#   zsh build-btidspoof.sh   -> build/BTIDSpoof.kext   (copy into EFI/OC/Kexts; see macos/bluetooth/README.md)
set -e
ROOT=${0:A:h}
SRC=$ROOT/macos/bluetooth/src
DEPS=$ROOT/build/deps
OUT=$ROOT/build/BTIDSpoof.kext
mkdir -p $DEPS
[[ -d $DEPS/MacKernelSDK ]] || git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git $DEPS/MacKernelSDK
SDK=$DEPS/MacKernelSDK
SYS=$(xcrun --show-sdk-path)
COMMON=(-arch x86_64 -mmacosx-version-min=13.0 -isysroot $SYS -nostdinc -mkernel -fno-stack-protector -DKERNEL -DKERNEL_PRIVATE -DAPPLE -I$SDK/Headers)
TMP=$(mktemp -d)
rm -rf $OUT; mkdir -p $OUT/Contents/MacOS
cp $SRC/Info.plist $OUT/Contents/Info.plist
xcrun clang++ $COMMON -std=gnu++17 -O2 -fapple-kext -fno-rtti -fno-exceptions -Wall -Wextra -c $SRC/BTIDSpoof.cpp -o $TMP/BTIDSpoof.o
xcrun clang $COMMON -c $SRC/kmod_info.c -o $TMP/kmod_info.o
xcrun clang++ -arch x86_64 -mmacosx-version-min=13.0 -isysroot $SYS -nostdlib -Xlinker -kext -L$SDK/Library/x86_64 \
  $TMP/BTIDSpoof.o $TMP/kmod_info.o -lkmod -lcc_kext -o $OUT/Contents/MacOS/BTIDSpoof
codesign --force --sign - $OUT
plutil -lint $OUT/Contents/Info.plist
echo "built $OUT"

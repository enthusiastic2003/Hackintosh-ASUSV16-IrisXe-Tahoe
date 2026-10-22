#!/bin/zsh
# sudo zsh tools/xe-gpuload-install.sh            install the boot-time accelerator loader (takes effect at the next boot)
# sudo zsh tools/xe-gpuload-install.sh desfazer   remove it
set -e
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
D=${0:A:h}; P=/Library/LaunchDaemons/local.xe.gpuload.plist; S=/usr/local/libexec/xe-gpuload.sh
if [[ ${1:-} == desfazer ]]; then rm -f $P $S; echo "removed"; exit 0; fi
mkdir -p /usr/local/libexec
install -o root -g wheel -m 755 $D/xe-gpuload.sh $S
install -o root -g wheel -m 644 $D/local.xe.gpuload.plist $P
plutil -lint $P
echo "installed: $S + $P. It runs at every boot; log: /Users/Shared/xe-live/gpuload.log"

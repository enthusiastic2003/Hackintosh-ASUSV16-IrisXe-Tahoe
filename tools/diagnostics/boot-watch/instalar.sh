#!/bin/zsh
# sudo zsh tools/diagnostics/boot-watch/instalar.sh            install (takes effect on the next boot)
# sudo zsh tools/diagnostics/boot-watch/instalar.sh desfazer   remove
set -e
D=${0:A:h}; P=/Library/LaunchDaemons/local.xe.bootwatch.plist; S=/usr/local/libexec/xe-boot-watch.sh
if [[ ${1:-} == desfazer ]]; then rm -f $P $S; echo "removed"; exit 0; fi
mkdir -p /usr/local/libexec
install -o root -g wheel -m 755 $D/xe-boot-watch.sh $S
install -o root -g wheel -m 644 $D/local.xe.bootwatch.plist $P
plutil -lint $P
echo "installed. Output goes to /Users/Shared/xe-boot/<boot time>/ on every normal boot (read it from safe mode)."

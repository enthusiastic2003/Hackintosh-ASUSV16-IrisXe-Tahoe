#!/bin/zsh
# sudo zsh tools/xe-wsrestart-run.sh --dry    checks and snapshots through launchd, restarts NOTHING (prove the machinery first)
# sudo zsh tools/xe-wsrestart-run.sh          restarts WindowServer in ~10 s (you are logged out; log back in)
# Installs the script + helpers under /usr/local/libexec/xe-ws and bootstraps a ONE-SHOT job into the SYSTEM launchd domain (a plain
# 'launchctl submit' from a terminal lands in your gui domain, fails and dies with your session). The plist lives outside
# /Library/LaunchDaemons so it does not run again at the next boot. Results: /Users/Shared/xe-live/wsrestart-<time>/log.txt
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
set -e
D=${0:A:h}; R=${D:h}
X=/usr/local/libexec/xe-ws; J=/Library/xe-wsrestart; L=local.xe.wsrestart
[[ -x $R/build/xe-metal-devices ]] || { echo "missing $R/build/xe-metal-devices (build it first)"; exit 1; }
mkdir -p $X $J /Users/Shared/xe-live
install -o root -g wheel -m 755 $D/xe-wsrestart.sh $X/xe-wsrestart.sh
install -o root -g wheel -m 755 $D/xe-live-log.py $X/xe-live-log.py
install -o root -g wheel -m 755 $R/build/xe-metal-devices $X/xe-metal-devices
chown root:wheel $J; chmod 755 $J
ENV=""; [[ ${1:-} == --dry ]] && ENV='<key>EnvironmentVariables</key><dict><key>XE_DRYRUN</key><string>1</string></dict>'
cat > $J/$L.plist <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$L</string>
<key>ProgramArguments</key><array><string>/bin/zsh</string><string>$X/xe-wsrestart.sh</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><false/>
<key>StandardOutPath</key><string>/Users/Shared/xe-live/wsrestart-launchd.out</string>
<key>StandardErrorPath</key><string>/Users/Shared/xe-live/wsrestart-launchd.err</string>
$ENV
</dict></plist>
PL
chown root:wheel $J/$L.plist; chmod 644 $J/$L.plist; plutil -lint $J/$L.plist >/dev/null
launchctl bootout system/$L 2>/dev/null || true
launchctl bootstrap system $J/$L.plist
if [[ ${1:-} == --dry ]]; then echo "dry run started in the system domain; read /Users/Shared/xe-live/wsrestart-*/log.txt in ~10 s. Nothing is restarted."
else echo "started. WindowServer restarts in ~10 s: save your work; you will be logged out. Log back in; results in /Users/Shared/xe-live/wsrestart-*/log.txt"; fi

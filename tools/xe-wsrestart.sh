#!/bin/zsh
# Restart WindowServer so it enumerates the Metal device, and record what happened. Runs as a ROOT job under launchd (see
# xe-wsrestart-run.sh), so it survives the logout that restarting WindowServer causes. Output: /Users/Shared/xe-live/wsrestart-<time>/
# Guards: safe boot, no registered accelerator, or no Metal device -> nothing is restarted.   XE_DRYRUN=1 only checks.
umask 022
X=${0:A:h}   # installed by xe-wsrestart-run.sh together with xe-metal-devices and xe-live-log.py
D=/Users/Shared/xe-live/wsrestart-$(date +%Y%m%d-%H%M%S); mkdir -p $D; exec >>$D/log.txt 2>&1
say() { print -r -- "$(date '+%H:%M:%S') $*"; sync; }
snap() { # $1 = label
  say "--- snapshot $1"
  ps -axo pid,pcpu,etime,comm | grep -E '[W]indowServer$'
  local pid=$(pgrep -x WindowServer | head -1)
  [[ -n $pid ]] && { say "WindowServer pid $pid: Metal driver / xedrv mapped:"; lsof -p $pid 2>/dev/null | grep -i -E 'TGLGraphicsMTLDriver|libxedrv|libigdmd|GPUBundles' | awk '{print $9}' | sort -u | head -6; }
  say "IntelAccelerator clients: $(ioreg -l -w0 -r -c IntelAccelerator 2>/dev/null | grep -c -E 'IGAccel|IOAccelContext|IOAccelSharedUserClient')"
  ioreg -r -c IntelAccelerator 2>/dev/null | grep -E '^\+-o' | cut -c1-120
}
say "== xe-wsrestart (uptime $(( $(date +%s) - $(sysctl -n kern.boottime | sed -n 's/^{ sec = \([0-9]*\),.*/\1/p') )) s)"
[[ $(sysctl -n kern.safeboot) == 1 ]] && { say "safe boot: abort"; exit 0; }
ioreg -r -c IntelAccelerator 2>/dev/null | grep -q 'registered, matched, active' || { say "accelerator not registered: abort"; exit 0; }
n=$(perl -e 'alarm 30; exec @ARGV' $X/xe-metal-devices list 2>&1 | grep -c "device(s)\|name=")
say "metal probe lines: $n"
perl -e 'alarm 30; exec @ARGV' $X/xe-metal-devices list 2>&1 | grep -q "1 device" || { say "no Metal device: abort"; exit 0; }
snap before
[[ -n $XE_DRYRUN ]] && { say "dry run: would restart WindowServer now"; exit 0; }
# keep the kernel log (128 KB ring) while this happens
python3 -I $X/xe-live-log.py >/dev/null 2>&1 & LOGPID=$!
sleep ${XE_DELAY:-10}
old=$(pgrep -x WindowServer | head -1)
# killall's default SIGTERM is ignored by WindowServer (pid unchanged): SIGHUP is the usual restart, SIGKILL the fallback
say "sending SIGHUP to WindowServer pid $old"; killall -HUP WindowServer; say "killall -HUP rc=$?"
for i in {1..8}; do sleep 1; [[ $(pgrep -x WindowServer | head -1) != $old ]] && break; done
if [[ $(pgrep -x WindowServer | head -1) == $old ]]; then say "pid $old still alive: sending SIGKILL"; killall -KILL WindowServer; say "killall -KILL rc=$?"; fi
for i in {1..90}; do new=$(pgrep -x WindowServer | head -1); [[ -n $new && $new != $old ]] && break; sleep 1; done
say "WindowServer restarted: old pid $old -> new pid ${new:-none} (waited ~${i}s)"
sleep 25; snap "after 25s"
sleep 35; snap "after 60s"
say "--- WindowServer / Metal log lines in the last 2 minutes:"
/usr/bin/log show --last 2m --style compact --predicate 'process == "WindowServer" OR process == "MTLCompilerService" OR eventMessage CONTAINS[c] "IGAccel"' 2>/dev/null | grep -i -E 'metal|error|fail|timeout|gpu|device' | tail -25 | cut -c1-220
kill $LOGPID 2>/dev/null
say "== done"

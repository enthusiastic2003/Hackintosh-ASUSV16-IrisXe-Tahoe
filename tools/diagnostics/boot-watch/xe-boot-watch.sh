#!/bin/zsh
# xe-boot-watch: records what happens during a boot that never reaches a desktop (LaunchDaemon local.xe.bootwatch, root).
# Every few seconds for ~10 minutes it rewrites, with sync (so it survives a forced power-off):
#   dmesg (kernel ring buffer: Lilu / NootedGreen SYSLOG lines, "ngreen", "topology:"), kmutil showloaded (filtered),
#   GPU registry objects, and the process list. At 45 s and 120 s it adds a spindump of WindowServer / loginwindow.
# Output: /Users/Shared/xe-boot/<boot time>/  . Read it from a safe-mode boot (-x does not start third-party daemons).
OUT=/Users/Shared/xe-boot/$(date +%Y%m%d-%H%M%S); mkdir -p $OUT
T0=$SECONDS
{ echo "boot-args: $(sysctl -n kern.bootargs)"; sw_vers; sysctl -n kern.boottime; } > $OUT/info.txt 2>&1
while (( SECONDS - T0 < 900 )); do
  t=$(( SECONDS - T0 ))
  # The kernel message buffer is a 128 KB ring (kern.msgbuf; the msgbuf= boot-arg is ignored) and wraps in seconds under log spam.
  # Keep a growing copy: append only the lines newer than the last timestamp seen.
  last=$(cat $OUT/.lastts 2>/dev/null || echo 0)
  dmesg 2>/dev/null | awk -v last="$last" -v tsf="$OUT/.lastts" '
    { if (match($0, /^\[ *[0-9]+\.[0-9]+\]/)) ts = substr($0, RSTART + 1, RLENGTH - 2) + 0 }
    ts > last { print; if (ts > mx) mx = ts }
    END { if (mx > 0) print mx > tsf }' >> $OUT/dmesg-all.txt
  dmesg > $OUT/dmesg.txt.new 2>&1 && mv $OUT/dmesg.txt.new $OUT/dmesg.txt
  { echo "-- t=${t}s $(date +%T)"; kmutil showloaded --list-only 2>&1 | grep -i -E 'green|lilu|hookcase|AppleIntel|IOAccel|IOGraphics|Voodoo|WhateverGreen'; } > $OUT/kexts.txt 2>&1
  { echo "-- t=${t}s"; ioreg -l -w0 2>/dev/null | grep -i -E 'IntelAccelerator|IGAccelDevice|AppleIntelFramebuffer|IOAccelerator|AGPM|"IOGLBundleName"|MetalPluginName|display0|AppleDisplay'; } > $OUT/gpu-objects.txt 2>&1
  { echo "-- t=${t}s"; ps -axo pid=,stat=,etime=,pcpu=,comm= | grep -E 'WindowServer|loginwindow|MTLCompiler|mds|kernelmanagerd|Dock|Finder|coreauthd|logd' ; } > $OUT/procs.txt 2>&1
  if (( t >= 45 && t < 49 )) || (( t >= 120 && t < 124 )); then
    for p in WindowServer loginwindow; do spindump $p 3 10 -stdout > $OUT/spindump-$p-${t}s.txt 2>&1; done
  fi
  sync
  sleep 1
done
echo "done $(date +%T)" >> $OUT/info.txt; sync

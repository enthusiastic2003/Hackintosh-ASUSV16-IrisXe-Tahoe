#!/bin/zsh
# The risky step, with markers in the live log:   sudo zsh tools/xe-live-load.sh
# Needs tools/xe-live-log.py running in another terminal. Loads the Tiger Lake accelerator kext by hand (no matching); NootedGreen
# reacts to the load and, with ngreeninjectdelay=N in the boot-args, injects the accelerator personality N s later.
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(sysctl -n kern.safeboot) == 0 ]] || { echo "refusing: safe boot"; exit 1; }
pgrep -f xe-live-log.py >/dev/null || { echo "start the live logger first: sudo python3 tools/xe-live-log.py"; exit 1; }
LOG=$(tail -1 /Users/Shared/xe-live/CURRENT)
mark() { python3 -I - "$LOG" "$1" <<'PYEOF'
import fcntl, os, sys, time
fd = os.open(sys.argv[1], os.O_WRONLY | os.O_APPEND)
os.write(fd, f"### MARK {time.strftime('%H:%M:%S')} {sys.argv[2]}\n".encode()); fcntl.fcntl(fd, fcntl.F_FULLFSYNC)
PYEOF
}
echo "log: $LOG"; echo "Caps Lock is your heartbeat: toggle it after each step. Starting in 5 s (Ctrl-C to abort)..."; sleep 5
mark "kmutil load --load-style start-only: begin"
/usr/bin/kmutil load -z -b com.xxxxx.driver.AppleIntelTGLGraphics --load-style start-only
rc=$?
mark "kmutil returned rc=$rc"
echo "kmutil returned $rc. NootedGreen will inject the personality after the configured delay; watch the log and the Caps Lock LED."

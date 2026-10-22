#!/usr/bin/env python3
# Run as root BEFORE a risky step; keep it open in its own terminal:   sudo python3 tools/xe-live-log.py
# Appends every new kernel-log line (dmesg; the ring is only 128 KB) to /Users/Shared/xe-live/dmesg-live-<time>.txt every 0.05 s
# with F_FULLFSYNC, so after a hard hang the last lines (and a '# hb' heartbeat line every 2 s) are on disk. Ctrl-C stops it.
import fcntl, os, re, subprocess, sys, time
if os.geteuid() != 0:
    sys.exit("run with sudo")
OUT = "/Users/Shared/xe-live"
os.makedirs(OUT, exist_ok=True)
path = f"{OUT}/dmesg-live-{time.strftime('%Y%m%d-%H%M%S')}.txt"
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
open(f"{OUT}/CURRENT", "w").write(path + "\n")
TS = re.compile(r"^\[\s*(\d+\.\d+)\]")
last, hb = 0.0, 0.0
print("logging to", path, "(Ctrl-C to stop)", flush=True)
def put(text):
    os.write(fd, text.encode("utf-8", "replace"))
    fcntl.fcntl(fd, fcntl.F_FULLFSYNC)
try:
    while True:
        out = subprocess.run(["dmesg"], capture_output=True).stdout.decode("utf-8", "replace")
        cur, chunk = last, []
        for line in out.split("\n"):
            m = TS.match(line)
            if m:
                cur = float(m.group(1))
            if cur > last and line:
                chunk.append(line)
        if chunk:
            put("\n".join(chunk) + "\n")
            last = cur
        now = time.time()
        if now - hb > 2:
            put(f"# hb {time.strftime('%H:%M:%S')} last-kernel-ts {last:.3f}\n")
            hb = now
        time.sleep(0.05)
except KeyboardInterrupt:
    put("# logger stopped\n")

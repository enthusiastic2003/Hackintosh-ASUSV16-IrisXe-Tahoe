#!/usr/bin/env python3
# add-load-dylib.py IN OUT PATH: copy a thin x86_64 Mach-O dylib and append an LC_LOAD_DYLIB for PATH in the header padding.
# The code signature becomes invalid; re-sign the output (codesign -f -s -).
import struct, sys
src, dst, path = sys.argv[1:4]
d = bytearray(open(src, 'rb').read())
magic, cpu, sub, ft, nc, sz, fl, res = struct.unpack_from('<IiiIIIII', d, 0)
assert magic == 0xfeedfacf, "not a thin 64-bit Mach-O"
raw = path.encode() + b'\0'
cmdsize = (24 + len(raw) + 7) & ~7
p = 32; first = 1 << 62
for i in range(nc):
    c, s = struct.unpack_from('<II', d, p)
    if c == 0x19:
        n = struct.unpack_from('<I', d, p + 64)[0]
        for j in range(n):
            off = struct.unpack_from('<I', d, p + 72 + 80 * j + 48)[0]
            if off: first = min(first, off)
    if c in (0xc, 0x8000001c, 0x80000018) and d[p + struct.unpack_from('<I', d, p + 8)[0]:p + s].split(b'\0')[0] == path.encode():
        sys.exit("already has %s" % path)
    p += s
end = 32 + sz
if first - end < cmdsize or any(d[end:end + cmdsize]):
    sys.exit("no room in the header (%d bytes free, %d needed)" % (first - end, cmdsize))
d[end:end + cmdsize] = struct.pack('<IIIIII', 0xc, cmdsize, 24, 2, 0x10000, 0x10000) + raw + b'\0' * (cmdsize - 24 - len(raw))
struct.pack_into('<II', d, 16, nc + 1, sz + cmdsize)
open(dst, 'wb').write(d)
print("added LC_LOAD_DYLIB %s (%d bytes of %d free)" % (path, cmdsize, first - end))

import struct, sys, glob
core = open(sys.argv[1], 'rb').read(); root = sys.argv[2]; addrs = [int(a, 16) for a in sys.argv[3:]]
e = struct.unpack('<16sHHIQQQIHHHHHH', core[:64])
segs = []
for i in range(e[10]):
    p = struct.unpack('<IIQQQQQQ', core[e[5] + i*56: e[5] + i*56 + 56])
    if p[0] == 1: segs.append((p[3], p[2], p[5]))
def rd(va, n):
    for v, off, sz in segs:
        if v <= va and va + n <= v + sz: return core[off + va - v: off + va - v + n]
files = glob.glob(root + '/usr/lib/x86_64-gnu/*.so*') + glob.glob(root + '/usr/lib/ld-x86-64.so.1') + [root + '/sbin/dhclient', root + '/usr/bin/dash']
blobs = {f: open(f, 'rb').read() for f in files if not f.endswith('.a')}
for a in addrs:
    probe = rd(a - 32, 64)
    if not probe: print("%#x: not in core" % a); continue
    for f, b in blobs.items():
        k = b.find(probe)
        if k >= 0:
            print("%#x: %s file offset %#x -> base %#x (if offset==vaddr)" % (a, f.split('/')[-1], k + 32, a - (k + 32)))

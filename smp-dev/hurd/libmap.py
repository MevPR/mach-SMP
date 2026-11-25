# Find which sysroot ELF files are mapped where in a core, by page contents.
import struct, sys, glob, hashlib, os
core = open(sys.argv[1], 'rb').read(); root = sys.argv[2]
e = struct.unpack('<16sHHIQQQIHHHHHH', core[:64])
pages = {}
files = glob.glob(root + '/usr/lib/x86_64-gnu/*.so*') + glob.glob(root + '/usr/lib/ld-x86-64.so.1') + glob.glob(root + '/usr/lib/x86_64-gnu/*/*.so*')
files += [f for f in glob.glob(root + '/hurd/*') + glob.glob(root + '/sbin/*') + glob.glob(root + '/usr/bin/*') if os.path.isfile(f)]
for f in files:
    try: b = open(f, 'rb').read()
    except Exception: continue
    if b[:4] != b'\x7fELF': continue
    for off in range(0, len(b) - 4095, 4096):
        pg = b[off:off + 4096]
        if pg.count(0) > 3000: continue
        pages.setdefault(hashlib.md5(pg).digest(), (f, off))
votes = {}
for i in range(e[10]):
    p = struct.unpack('<IIQQQQQQ', core[e[5] + i*56: e[5] + i*56 + 56])
    if p[0] != 1: continue
    for k in range(0, p[5], 4096):
        h = hashlib.md5(core[p[2] + k: p[2] + k + 4096]).digest()
        if h in pages:
            f, off = pages[h]
            base = p[3] + k - off
            votes[(f, base)] = votes.get((f, base), 0) + 1
for (f, base), n in sorted(votes.items(), key=lambda x: -x[1]):
    if n >= 2:
        print("%s %#x %d" % (f.replace(root, ''), base, n))

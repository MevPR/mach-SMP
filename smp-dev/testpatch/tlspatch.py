#!/usr/bin/env python3
"""Generic binary patch for the glibc Hurd multiple_threads bug.

  tlspatch.py [--all] <input ELF> <output ELF>

With --all, every such store is patched (glibc 2.43 has two: in
pthread_create and in mach_setup_tls); without it, exactly one is
required.

Finds, in a glibc libc.so / libpthread.so or in a static Hurd program,
the store `tcb->tcb = tcb` (mov %rax,(%rax)) that pthread_create does
right after _dl_allocate_tls, and makes it also set
tcbhead_t.multiple_threads (movl $1,0x18(%rax)), like
outbox/glibc/0002 does in the source.  The store and the instructions
after it, up to at least 5 bytes, are moved to a cave at the end of the
executable segment (the padding up to the next page), followed by a jump
back; the original place gets a jump to the cave.

Refuses to patch when it is not sure: the pattern must be unique, the
moved instructions must not be RIP-relative or branches, and no branch
may target them.  Needs objdump (binutils).
"""
import re
import struct
import subprocess
import sys

CAVE_CODE_EXTRA = bytes.fromhex('c7401801000000')   # movl $0x1,0x18(%rax)


def objdump(path):
    out = subprocess.run(['objdump', '-d', '-w', path], capture_output=True,
                         text=True, check=True).stdout
    insns = []
    for line in out.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t?(.*)', line)
        if m:
            insns.append((int(m.group(1), 16),
                          bytes.fromhex(m.group(2).replace(' ', '')),
                          m.group(3).strip()))
    return insns


def segments(data):
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    segs = []
    for i in range(phnum):
        p_type, flags, off, va, pa, fsz, msz, al = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phentsize)
        if p_type == 1:
            segs.append((flags, off, va, fsz, msz))
    return segs


def find_sites(insns):
    sites = []
    for i, (a, b, t) in enumerate(insns):
        if t.startswith('call') and '_dl_allocate_tls' in t and 'init' not in t:
            for j in range(i + 1, min(i + 13, len(insns))):
                if insns[j][2] == 'mov    %rax,(%rax)':
                    sites.append(j)
                    break
    return sites


def patch_site(data, insns, j, cave_va):
    start = insns[j][0]
    moved, k = [], j
    while sum(len(b) for _, b, _ in moved) < 5:
        a, b, t = insns[k]
        if '%rip' in t or t.split()[0].startswith(('j', 'call', 'ret', 'loop')):
            sys.exit('cannot move instruction at %#x: %s' % (a, t))
        moved.append(insns[k])
        k += 1
    end = insns[k][0]
    length = end - start
    for a, b, t in insns:
        m = re.match(r'(j\w+|call)\s+([0-9a-f]+)\b', t)
        if m and start < int(m.group(2), 16) < end:
            sys.exit('branch at %#x targets moved range' % a)
    for flags, off, va, fsz, msz in segments(data):
        if flags & 1 and va <= start < va + fsz:
            break
    else:
        sys.exit('no executable segment contains %#x' % start)
    code = moved[0][1] + CAVE_CODE_EXTRA + b''.join(b for _, b, _ in moved[1:])
    code += b'\xe9' + struct.pack('<i', end - (cave_va + len(code) + 5))
    cave_off = off + (cave_va - va)
    if any(data[cave_off:cave_off + len(code)]):
        sys.exit('cave area is not empty')
    data[cave_off:cave_off + len(code)] = code
    jmp = b'\xe9' + struct.pack('<i', cave_va - (start + 5))
    file_start = off + (start - va)
    data[file_start:file_start + length] = jmp + b'\x90' * (length - 5)
    print('  store at %#x (%d bytes moved) -> cave at %#x' % (start, length, cave_va))
    return (cave_va + len(code) + 15) & ~15, off, va


def main(src, dst, patch_all):
    data = bytearray(open(src, 'rb').read())
    insns = objdump(src)
    sites = find_sites(insns)
    if not sites or (len(sites) != 1 and not patch_all):
        sys.exit('expected exactly one tcb->tcb store after _dl_allocate_tls, found %d' % len(sites))
    # all sites must be in the same executable segment; caves follow its end
    seg = [s for s in segments(data) if s[0] & 1 and s[2] <= insns[sites[0]][0] < s[2] + s[3]][0]
    flags, off, va, fsz, msz = seg
    cave = (va + fsz + 15) & ~15
    page_end = (va + fsz + 0xfff) & ~0xfff
    for j in sites:
        cave, off, va = patch_site(data, insns, j, cave)
    if cave > page_end:
        sys.exit('caves do not fit before the end of the page')
    others = [o for f, o, v, s2, m in segments(data) if o > off]
    if others and off + (cave - va) > min(others):
        sys.exit('caves would overlap the next segment in the file')
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    for i in range(phnum):
        e = phoff + i * phentsize
        p_type, f2, o, v, pa, fsz2, msz2, al = struct.unpack_from('<IIQQQQQQ', data, e)
        if p_type == 1 and o == off and v == va:
            struct.pack_into('<QQ', data, e + 32, max(fsz2, cave - va), max(msz2, cave - va))
    open(dst, 'wb').write(data)
    print('patched %s: %d site(s)' % (dst, len(sites)))


if __name__ == '__main__':
    args = sys.argv[1:]
    patch_all = bool(args) and args[0] == '--all'
    if patch_all:
        args = args[1:]
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], patch_all)

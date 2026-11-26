#!/usr/bin/env python3
"""Generic binary patch for the glibc Hurd multiple_threads bug.

  tlspatch.py <input ELF> <output ELF>

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


def main(src, dst):
    data = bytearray(open(src, 'rb').read())
    insns = objdump(src)
    byaddr = {a: i for i, (a, _, _) in enumerate(insns)}

    # 1. find `call ..._dl_allocate_tls...` followed within 12 instructions
    #    by `mov %rax,(%rax)`.
    sites = []
    for i, (a, b, t) in enumerate(insns):
        if t.startswith('call') and '_dl_allocate_tls' in t and 'init' not in t:
            for j in range(i + 1, min(i + 13, len(insns))):
                if insns[j][2] == 'mov    %rax,(%rax)':
                    sites.append(j)
                    break
    if len(sites) != 1:
        sys.exit('expected exactly one tcb->tcb store after _dl_allocate_tls, found %d' % len(sites))
    j = sites[0]
    start = insns[j][0]

    # 2. take whole instructions from the store until >= 5 bytes.
    moved, k = [], j
    while sum(len(b) for _, b, _ in moved) < 5:
        a, b, t = insns[k]
        if '%rip' in t or t.split()[0].startswith(('j', 'call', 'ret', 'loop')):
            sys.exit('cannot move instruction at %#x: %s' % (a, t))
        moved.append(insns[k])
        k += 1
    end = insns[k][0]
    length = end - start

    # 3. no branch into the moved range (except its first instruction).
    for a, b, t in insns:
        m = re.match(r'(j\w+|call)\s+([0-9a-f]+)\b', t)
        if m:
            tgt = int(m.group(2), 16)
            if start < tgt < end:
                sys.exit('branch at %#x targets moved range (%#x)' % (a, tgt))

    # 4. file offset of START, and a cave at the end of its executable
    #    segment, inside the last page.
    for flags, off, va, fsz, msz in segments(data):
        if flags & 1 and va <= start < va + fsz:
            break
    else:
        sys.exit('no executable segment contains %#x' % start)
    file_start = off + (start - va)
    code = (CAVE_CODE_EXTRA.join([moved[0][1], b''])
            + b''.join(b for _, b, _ in moved[1:]))
    code = moved[0][1] + CAVE_CODE_EXTRA + b''.join(b for _, b, _ in moved[1:])
    cave_va = (va + fsz + 15) & ~15
    cave_off = off + (cave_va - va)
    need = len(code) + 5
    page_end = (va + fsz + 0xfff) & ~0xfff
    if cave_va + need > page_end:
        sys.exit('no room after the executable segment')
    others = [o for f, o, v, s, m in segments(data) if o > off]
    if others and cave_off + need > min(others):
        sys.exit('cave would overlap the next segment in the file')
    if any(data[cave_off:cave_off + need]):
        sys.exit('cave area is not empty')

    # 5. write the cave, the jump to it, and grow the segment to cover it.
    jmp_back = b'\xe9' + struct.pack('<i', end - (cave_va + len(code) + 5))
    data[cave_off:cave_off + need] = code + jmp_back
    jmp = b'\xe9' + struct.pack('<i', cave_va - (start + 5))
    data[file_start:file_start + length] = jmp + b'\x90' * (length - 5)
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    for i in range(phnum):
        e = phoff + i * phentsize
        p_type, flags, o, v, pa, fsz2, msz2, al = struct.unpack_from('<IIQQQQQQ', data, e)
        if p_type == 1 and o == off and v == va:
            newsz = (cave_va + need) - va
            struct.pack_into('<QQ', data, e + 32, max(fsz2, newsz), max(msz2, newsz))
    open(dst, 'wb').write(data)
    print('patched %s: store at %#x (%d bytes moved), cave at %#x' % (dst, start, length, cave_va))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])

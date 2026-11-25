# gdb python: write an x86_64 ELF core file of a Hurd task from the
# gnumach kernel's point of view (vm_map + pcb), so that a plain gdb with
# the right sysroot and debug files can unwind its user threads.
#
# Usage inside gdb attached to QEMU:
#   python CORE_TASK = "/bin/sh(262)"; CORE_OUT = "/hb/sh.core"
#   source /hb/mkcore.py
import gdb, struct
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])

KBASE = 0xffffffff80000000
inf = gdb.selected_inferior()

def phys_mode(on):
    gdb.execute("maintenance packet Qqemu.PhyMemMode:%d" % (1 if on else 0), to_string=True)

def prd(pa, n):
    return bytes(inf.read_memory(pa, n))

def translate_phys(l4, va):
    pt = None
    for level, shift in ((4, 39), (3, 30), (2, 21), (1, 12)):
        if level == 4:
            e = l4[(va >> shift) & 0x1ff]
        else:
            e = struct.unpack('<Q', prd(pt + ((va >> shift) & 0x1ff) * 8, 8))[0]
        if not e & 1:
            return None
        pa = e & 0x000ffffffffff000
        if level in (3, 2) and e & 0x80:
            size = 1 << shift
            return (pa & ~(size - 1)) + (va & (size - 1))
        pt = pa
    return pa + (va & 0xfff)

def find_task(name):
    for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
        if task_name(task) == name:
            return task
    raise gdb.GdbError("no task " + name)

task = find_task(CORE_TASK)
vmmap = task['map']
l4 = struct.unpack('<512Q', bytes(inf.read_memory(int(vmmap['pmap']['l4base']), 4096)))

# Collect regions (kernel-virtual reads).
regions = []
hdr = vmmap['hdr']
e = hdr['links']['next']
n = 0
while int(e) != int(hdr.address) and n < 100000:
    regions.append((int(e['links']['start']), int(e['links']['end']),
                    int(e['protection']) if 'protection' in [f.name for f in e.type.target().fields()] else 7))
    e = e['links']['next']; n += 1

# Collect thread register states.
threads = []
for th in entries(task['thread_list'], "struct thread", "thread_list"):
    iss = th['pcb']['iss']
    g = lambda f: int(iss[f]) & 0xffffffffffffffff
    fsbase = int(th['pcb']['ims']['sbs']['fsbase'])
    gsbase = int(th['pcb']['ims']['sbs']['gsbase'])
    regs = [g('r15'), g('r14'), g('r13'), g('r12'), g('ebp'), g('ebx'), g('r11'),
            g('r10'), g('r9'), g('r8'), g('eax'), g('ecx'), g('edx'), g('esi'),
            g('edi'), g('eax'), g('eip'), g('cs'), g('efl'), g('uesp'), g('ss'),
            fsbase, gsbase, 0, 0, 0, 0]
    threads.append((int(th), regs))

# Read memory of present pages, coalesced into runs (physical mode).
segs = []
phys_mode(True)
try:
    for start, end, prot in regions:
        va = start
        while va < end:
            pa = translate_phys(l4, va)
            if pa is None:
                va += 0x1000
                continue
            data = prd(pa, 0x1000)
            if segs and segs[-1][0] + len(segs[-1][1]) == va:
                segs[-1][1].extend(data)
            else:
                segs.append((va, bytearray(data), prot))
            va += 0x1000
finally:
    phys_mode(False)

def find_elf_base(want):
    for va, data, prot in segs:
        if data[:4] == b'\x7fELF' and want(data):
            return va, data
    return None, None

# Auxv: locate the program (ET_DYN with PT_INTERP) and the dynamic linker.
auxv = []
def ehdr(data):
    return struct.unpack('<16sHHIQQQIHHHHHH', bytes(data[:64]))
def has_interp(data):
    h = ehdr(data)
    if h[1] not in (2, 3):
        return False
    for i in range(h[10]):
        off = h[5] + i * h[9]
        if struct.unpack('<I', bytes(data[off:off + 4]))[0] == 3:
            return True
    return False
pbase, pdata = find_elf_base(has_interp)
if pbase is not None:
    h = ehdr(pdata)
    is_dyn = h[1] == 3
    auxv += [(3, (pbase if is_dyn else 0) + h[5]), (4, h[9]), (5, h[10]),
             (9, (pbase if is_dyn else 0) + h[4])]
try:
    auxv = list(CORE_AUXV)
except NameError:
    pass
auxv.append((0, 0))

def note(name, ntype, desc):
    name = name + b'\0'
    pad = lambda b: b + b'\0' * ((4 - len(b) % 4) % 4)
    return struct.pack('<III', len(name), len(desc), ntype) + pad(name) + pad(desc)

notes = b''
for tid, regs in threads:
    pr = bytearray(336)
    struct.pack_into('<I', pr, 32, tid & 0x7fffffff)  # pr_pid
    struct.pack_into('<27Q', pr, 112, *regs)
    notes += note(b'CORE', 1, bytes(pr))
notes += note(b'CORE', 6, b''.join(struct.pack('<QQ', a, v) for a, v in auxv))

phnum = 1 + len(segs)
off = 64 + 56 * phnum
out = bytearray()
out += struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                   4, 62, 1, 0, 64, 0, 0, 64, 56, phnum, 0, 0, 0)
out += struct.pack('<IIQQQQQQ', 4, 0, off, 0, 0, len(notes), 0, 4)
off += len(notes)
for va, data, prot in segs:
    out += struct.pack('<IIQQQQQQ', 1, 7, off, va, 0, len(data), len(data), 0x1000)
    off += len(data)
out += notes
for va, data, prot in segs:
    out += data
open(CORE_OUT, 'wb').write(out)
print("wrote %s: %d threads, %d segments, %d bytes; auxv %s" % (
    CORE_OUT, len(threads), len(segs), len(out),
    ' '.join('%d=%#x' % (a, v) for a, v in auxv)))

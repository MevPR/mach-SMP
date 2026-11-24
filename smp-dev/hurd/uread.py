# gdb python: read user memory of a task by walking its x86_64 page tables.
# Physical memory is reachable through phystokv (KERNEL_MAP_BASE + pa).
import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])

KBASE = 0xffffffff80000000
inf = gdb.selected_inferior()

def rd64(kva):
    return int.from_bytes(bytes(inf.read_memory(kva, 8)), 'little')

def translate(task, va):
    pt = int(task['map']['pmap']['l4base'])
    for level, shift in ((4, 39), (3, 30), (2, 21), (1, 12)):
        e = int.from_bytes(rdphys(pt + ((va >> shift) & 0x1ff) * 8, 8), 'little') if level != 4 else rd64(pt + ((va >> shift) & 0x1ff) * 8)
        if not e & 1:
            return None
        pa = e & 0x000ffffffffff000
        if level in (3, 2) and e & 0x80:   # 1G or 2M page
            size = 1 << shift
            return (pa & ~(size - 1)) + (va & (size - 1))
        pt = pa
    return pa + (va & 0xfff)

def rdphys(pa, n):
    gdb.execute("maintenance packet Qqemu.PhyMemMode:1", to_string=True)
    try:
        return bytes(inf.read_memory(pa, n))
    finally:
        gdb.execute("maintenance packet Qqemu.PhyMemMode:0", to_string=True)

def uread(task, va, n):
    out = b''
    while n > 0:
        pa = translate(task, va)
        chunk = min(n, 0x1000 - (va & 0xfff))
        if pa is None:
            out += b'\0' * chunk
        else:
            out += rdphys(pa, chunk)
        va += chunk; n -= chunk
    return out

def find_task(name):
    for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
        if task_name(task) == name:
            return task

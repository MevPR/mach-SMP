import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
inf = gdb.selected_inferior()
def rd(a): return int.from_bytes(bytes(inf.read_memory(a, 8)), 'little')
def sym(a):
    try: return gdb.execute("info symbol %#x" % a, to_string=True).strip()
    except Exception: return "?"
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    if name not in KS_TASKS: continue
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        ks = int(th['kernel_stack'])
        if not ks or int(th['state']) & 4: continue
        for size in (4096, 16384, 8192):
            iks = gdb.parse_and_eval("(struct i386_kernel_state *)(%d + %d) - 1" % (ks, size)).dereference()
            if 'in section .text' in sym(int(iks['k_eip'])):
                break
        rbp = int(iks['k_ebp'])
        out = ["[stack %d] " % size + sym(int(iks['k_eip']))]
        for i in range(16):
            if rbp < ks or rbp >= ks + size: break
            out.append(sym(rd(rbp + 8))); rbp = rd(rbp)
        print("%s %s state %#x user_rip %#x" % (name, th, int(th['state']), int(th['pcb']['iss']['eip'])))
        for o in out: print("    " + o.split(' in section')[0])

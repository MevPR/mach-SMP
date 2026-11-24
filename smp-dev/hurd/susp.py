import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
inf = gdb.selected_inferior()
def rd(a):
    return int.from_bytes(bytes(inf.read_memory(a, 8)), 'little')
def sym(a):
    try:
        return gdb.execute("info symbol %#x" % a, to_string=True).strip()
    except Exception:
        return "?"
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    if 'sh(262)' not in name:
        continue
    print("TASK %s %s suspend_count %d user_stop_count %d active %d" % (
        name, task, int(task['suspend_count']), int(task['user_stop_count']), int(task['active'])))
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        print("  thread %s state %#x suspend_count %d user_stop_count %d swap_func %s user_rip %#x" % (
            th, int(th['state']), int(th['suspend_count']), int(th['user_stop_count']),
            str(th['swap_func']).split()[-1], int(th['pcb']['iss']['eip'])))
        ks = int(th['kernel_stack'])
        if ks:
            iks = gdb.parse_and_eval("(struct i386_kernel_state *)(%d + 16384) - 1" % ks).dereference()
            rip = int(iks['k_eip']); rbp = int(iks['k_ebp'])
            print("    kernel: %s" % sym(rip))
            for i in range(14):
                if rbp < ks or rbp >= ks + 16384:
                    break
                ret = rd(rbp + 8)
                print("    <- %s" % sym(ret))
                rbp = rd(rbp)

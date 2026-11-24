import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        try:
            cont = str(th['swap_func'])
        except Exception as e:
            cont = "?"
        if 'mach_msg' in cont:
            continue
        ev = th['wait_event']
        try:
            evs = str(ev)
        except Exception:
            evs = hex(int(ev))
        print("%-16s thread %s state %#x swap_func %s wait_event %s kstack %#x" %
              (name, th, int(th['state']), cont.split()[-1], evs, int(th['kernel_stack'])))

import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
LINES = {}
el = int(gdb.parse_and_eval("elapsed_ticks"))
print("elapsed_ticks %d" % el)
found = 0
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        st = int(th['state']); opt = int(th['saved']['receive']['option'])
        tm = th['timer']
        if not (st & 1) or not (opt & 0x100) or int(tm['set']) & 6:
            continue
        found += 1
        print("%s thread %s state %#x opt %#x timeout %d timer.set %#x swap_func %s" % (
            name, th, st, opt, int(th['saved']['receive']['timeout']), int(tm['set']),
            str(th['swap_func']).split()[-1]))
        n = int(tm['h_idx'])
        for k in range(max(0, n - 16), n):
            i = k % 16
            print("   #%d tick %d cpu %d line %d set-before %#x extra %#x" % (
                k, int(tm['h_tick'][i]), int(tm['h_cpu'][i]), int(tm['h_line'][i]),
                int(tm['h_set'][i]), int(tm['h_extra'][i]) & 0xffffffff))
print("threads waiting with RCV_TIMEOUT but no armed timer: %d" % found)

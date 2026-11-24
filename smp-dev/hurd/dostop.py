import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
i = 0
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    ths = list(entries(task['thread_list'], "struct thread", "thread_list"))
    susp = [t for t in ths if int(t['state']) & 2]
    print("task %2d %-20s %s threads %d suspended %d task_suspend %d" % (i, name, task, len(ths), len(susp), int(task['suspend_count'])))
    if susp and name == 'ext2fs':
        for t in ths:
            if not (int(t['state']) & 2):
                print("   running/waiting thread %s state %#x swap_func %s obj %s user_rip %#x" % (
                    t, int(t['state']), str(t['swap_func']).split()[-1],
                    t['saved']['receive']['object'], int(t['pcb']['iss']['eip'])))
    i += 1

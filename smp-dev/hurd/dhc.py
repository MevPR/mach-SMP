import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
el = int(gdb.parse_and_eval("elapsed_ticks"))
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    if 'dhclient' not in name and 'pfinet' not in name and 'netdde' not in name:
        continue
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        r = th['saved']['receive']
        print("%-18s %s state %#x %-28s opt %#x timeout %d timer.set %d t_time %d (now %d) wait_event %s" % (
            name, th, int(th['state']), str(th['swap_func']).split()[-1], int(r['option']), int(r['timeout']),
            int(th['timer']['set']), int(th['timer']['t_time']), el, th['wait_event']))

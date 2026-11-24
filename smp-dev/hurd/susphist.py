import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
def sym(a):
    try:
        return gdb.execute("info symbol %#x" % a, to_string=True).strip()
    except Exception:
        return "?"
names = {}
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        names[int(th)] = task_name(task)
for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    name = task_name(task)
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        if not (int(th['state']) & 2):
            continue
        print("%s thread %s state %#x suspend_count %d user_stop_count %d" % (
            name, th, int(th['state']), int(th['suspend_count']), int(th['user_stop_count'])))
        n = int(th['susp_idx'])
        for k in range(max(0, n - 16), n):
            i = k % 16
            line = int(th['susp_line'][i]); val = int(th['susp_val'][i])
            by = int(th['susp_by'][i])
            print("   #%d %s line %d (count %d) caller %s by %s(%#x)" % (
                k, "+" if line > 0 else "-", abs(line), val, sym(int(th['susp_caller'][i])),
                names.get(by, "?"), by))

# gdb python: for every thread blocked in mach_msg, show its receive target
# and whether messages are already queued there (a lost wakeup).
import gdb

def entries(head, typ, field):
    # Mach queues link the element pointers themselves (queue_enter).
    t = gdb.lookup_type(typ).pointer()
    e = head['next']
    n = 0
    while int(e) != int(head.address) and n < 10000:
        elt = e.cast(t)
        yield elt
        e = elt[field]['next']
        n += 1

def task_name(task):
    raw = bytes(gdb.selected_inferior().read_memory(task['name'].address, 32))
    return raw.split(b'\0')[0].decode('latin-1')

lost = 0
total = 0

def describe(name, th, cont):
    global lost
    obj = th['saved']['receive']['object']
    if int(obj) == 0:
        print("%-16s thread %s %-26s no object" % (name, th, cont))
        return
    bits = int(obj['io_bits'])
    otype = (bits & 0x3fff0000) >> 16
    if otype == 0:
        tgt = obj.cast(gdb.lookup_type("struct ipc_port").pointer())['ip_cur_target']
    else:
        tgt = obj.cast(gdb.lookup_type("struct ipc_target").pointer())
    msgs = int(tgt['ipt_messages']['imq_messages']['ikmq_base'])
    waiters = int(tgt['ipt_messages']['imq_threads']['ithq_base'])
    flag = ""
    if msgs:
        flag = "  <<< MESSAGE QUEUED WHILE THREAD WAITS"
        lost += 1
    print("%-16s thread %s %-26s obj %s type %d msgs %#x waiters %#x%s" %
          (name, th, cont, obj, otype, msgs, waiters, flag))

for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
    try:
        name = task_name(task)
    except Exception as e:
        name = "?(%s)" % e
    for th in entries(task['thread_list'], "struct thread", "thread_list"):
        try:
            cont = str(th['swap_func']).split()[-1]
        except Exception as e:
            print("%-16s thread %s: %s" % (name, th, e))
            continue
        if 'mach_msg' not in cont:
            continue
        total += 1
        try:
            describe(name, th, cont)
        except Exception as e:
            print("%-16s thread %s %-26s error: %s" % (name, th, cont, e))

print("threads in mach_msg: %d, with queued messages: %d" % (total, lost))

import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
print("tot_num_intr", int(gdb.parse_and_eval("irqtab.tot_num_intr")))
q = gdb.parse_and_eval("main_intr_queue")
for e in entries(q, "user_intr_t", "chain"):
    i = int(e['id'])
    print("entry %s id %d n_unacked %d interrupts %d dst_port %s ndisabled %d" % (
        e, i, int(e['n_unacked']), int(e['interrupts']), e['dst_port'],
        int(gdb.parse_and_eval("nested_irqs[%d].ndisabled" % i))))
th = gdb.parse_and_eval("intr_thread_ptr") if False else None

import gdb
st = int(gdb.parse_and_eval("softticks")); el = int(gdb.parse_and_eval("elapsed_ticks"))
n = int(gdb.parse_and_eval("sizeof(timeoutwheel)/sizeof(timeoutwheel[0])"))
print("softticks %d elapsed_ticks %d wheel %d" % (st, el, n))
base = int(gdb.parse_and_eval("(unsigned long)&timeoutwheel[0]")); end = base + n * 16
tt = gdb.lookup_type("struct timeout").pointer()
overdue = 0; total = 0
for i in range(n):
    head = gdb.parse_and_eval("&timeoutwheel[%d]" % i)
    e = head['next']; k = 0
    while not (base <= int(e) < end) and k < 1000:
        t = e.cast(tt); total += 1
        tm = int(t['t_time'])
        if tm <= st:
            overdue += 1
            print("  OVERDUE timer %s t_time %d (softticks %d) fcn %s param %s" % (t, tm, st, t['fcn'], t['param']))
        e = t['chain']['next']; k += 1
print("timers in wheel: %d, overdue: %d" % (total, overdue))

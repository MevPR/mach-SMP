# gdb python: on the "deallocating a bogus port" printf in
# mach_port_deallocate, write an ELF core of the calling task (mkcore.py)
# and log the port name; then let the VM run on.
import gdb
exec(open('/hb/ipcwait.py').read().split('lost = 0')[0])
NHIT = [0]
class BogusBP(gdb.Breakpoint):
    def stop(self):
        NHIT[0] += 1
        space = gdb.parse_and_eval("space")
        name = int(gdb.parse_and_eval("name"))
        tname = None
        for task in entries(gdb.parse_and_eval("default_pset.tasks"), "struct task", "pset_tasks"):
            if int(task['itk_space']) == int(space):
                tname = task_name(task); break
        with open('/hb/bogus-log.txt', 'a') as f:
            f.write("hit %d: task %s name %d\n" % (NHIT[0], tname, name))
        if tname and NHIT[0] <= 3:
            gdb.execute('python CORE_TASK = %r; CORE_OUT = "/hb/bogus-%d.core"' % (tname, NHIT[0]))
            try:
                gdb.execute("source /hb/mkcore.py", to_string=True)
                with open('/hb/bogus-log.txt', 'a') as f:
                    f.write("  core /hb/bogus-%d.core written\n" % NHIT[0])
            except Exception as e:
                with open('/hb/bogus-log.txt', 'a') as f:
                    f.write("  core failed: %s\n" % e)
        return False
BogusBP("ipc/mach_port.c:593")

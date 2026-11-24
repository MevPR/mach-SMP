#!/usr/bin/env python3
# usage: catch.py <iso> <smp> <log> <idle-seconds> <max-seconds>
# Boot; if no login and no serial output for idle-seconds, enter ddb and dump state.
import pexpect, sys, time
iso, smp, logf, idle, maxs = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
log = open(logf, 'w')
cmd = ("qemu-system-x86_64 -m 2048 -smp %s -cpu core2duo-v1 -nographic -no-reboot -monitor none "
       "-drive file=/img/ov-catch.qcow2,format=qcow2,if=ide -cdrom %s -boot d -serial stdio" % (smp, iso))
p = pexpect.spawn(cmd, encoding='utf-8', codec_errors='replace', timeout=idle)
p.logfile_read = log
t0 = time.time()
while True:
    try:
        i = p.expect(['login:', 'db\\{\\d+\\}>', '\n'])
    except pexpect.TIMEOUT:
        print("no output for %ds at %.0fs: entering ddb" % (idle, time.time() - t0), flush=True)
        break
    if i == 0:
        print("reached login at %.0fs" % (time.time() - t0), flush=True); sys.exit(0)
    if i == 1:
        print("already in ddb", flush=True); break
    if time.time() - t0 > maxs:
        print("max time", flush=True); sys.exit(2)
p.send('\x1b\x04')
p.expect('db\\{\\d+\\}>', timeout=120)
def run(c):
    p.sendline(c)
    while p.expect(['db\\{\\d+\\}>', '--db_more--'], timeout=600) == 1:
        p.send(' ')
run('set $lines = 100000')
for c in ['show all tasks', 'show all threads', 'show all slocks']:
    run(c)
print("dumped", flush=True)
p.terminate(force=True)

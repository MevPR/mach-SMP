#!/usr/bin/env python3
# usage: drive.py <iso> <smp> <logfile> <cmdfile>
import pexpect, sys, time
iso, smp, logf, cmdf = sys.argv[1:5]
cmds = [l.rstrip('\n') for l in open(cmdf) if l.strip() and not l.startswith('#')]
log = open(logf, 'w')
qemu = ("qemu-system-x86_64 -m 2048 -smp %s -cpu core2duo-v1 -nographic -no-reboot -monitor none "
        "-drive file=/img/overlay.qcow2,format=qcow2,if=ide -cdrom %s -boot d -serial stdio" % (smp, iso))
t0 = time.time()
p = pexpect.spawn(qemu, encoding='utf-8', codec_errors='replace', timeout=1200)
p.logfile_read = log
def stamp(msg):
    log.write("\n#### [%6.1fs] %s\n" % (time.time() - t0, msg)); log.flush()
    print("[%6.1fs] %s" % (time.time() - t0, msg), flush=True)
i = p.expect(['login:', 'panic', 'Assertion', pexpect.TIMEOUT, pexpect.EOF])
if i != 0:
    stamp("boot failed (%d)" % i); sys.exit(1)
stamp("login prompt")
p.sendline('root')
i = p.expect(['assword:', r'# $', r'\$ $'], timeout=120)
if i == 0:
    p.sendline('')
p.expect(r'# ', timeout=300)
p.sendline("PS1='HURD# '")
p.expect('HURD# ', timeout=120)
p.expect('HURD# ', timeout=120)
for c in cmds:
    stamp("run: " + c)
    p.sendline(c)
    i = p.expect(['HURD# ', 'panic', 'Assertion', pexpect.TIMEOUT, pexpect.EOF], timeout=1800)
    out = p.before
    print(out.strip()[-1500:], flush=True)
    if i != 0:
        stamp("command failed (%d)" % i); sys.exit(2)
stamp("done")
p.sendline('halt')
try: p.expect(pexpect.EOF, timeout=120)
except Exception: pass

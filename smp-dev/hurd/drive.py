#!/usr/bin/env python3
# usage: drive.py <iso> <smp> <logfile> <cmdfile>
import pexpect, sys, time, os
iso, smp, logf, cmdf = sys.argv[1:5]
cmds = [l.rstrip('\n') for l in open(cmdf) if l.strip() and not l.startswith('#')]
log = open(logf, 'w')
qemu = ("qemu-system-x86_64 -m 2048 -smp %s -cpu core2duo-v1 -nographic -no-reboot -monitor none "
        "-drive file=%s,format=%s,if=ide -cdrom %s -boot d -serial stdio %s" % (smp, os.environ.get("OVERLAY", "/img/overlay.qcow2"), os.environ.get("DISKFMT", "qcow2"), iso, os.environ.get("QEMU_EXTRA", "")))
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
    p.sendline(os.environ.get('PASSWORD', ''))
p.expect(r'#', timeout=300)
time.sleep(5)
p.sendline("PS1='HURD''# '")
p.expect('HURD# ', timeout=300)
stamp("shell ready")
for c in cmds:
    stamp("run: " + c)
    p.sendline(c)
    i = p.expect(['HURD# ', 'panic', 'Assertion', pexpect.TIMEOUT, pexpect.EOF], timeout=int(os.environ.get('CMD_TIMEOUT', '1800')))
    out = p.before
    print(out.strip()[-1500:], flush=True)
    if i != 0:
        stamp("command failed (%d)" % i); sys.exit(2)
stamp("done")
p.sendline('halt')
try: p.expect(pexpect.EOF, timeout=120)
except Exception: pass

#!/usr/bin/env python3
# Boot the persistent dev VM, enable root ssh with password "hurd", then keep QEMU running.
import pexpect, sys, time, os
iso = sys.argv[1]
log = open(os.environ.get('LOG', '/hb/devvm-serial.log'), 'a')
img = os.environ.get('IMG', '/img/devvm.raw')
cmd = ("qemu-system-x86_64 -m %s -smp %s -cpu core2duo-v1 -nographic -no-reboot -monitor none "
       "%s -cdrom %s -boot d -serial stdio "
       "-nic user,model=e1000,hostfwd=tcp::2222-:22 %s" % (os.environ.get('MEM', '3072'), os.environ.get('SMP', '1'),
       (("-device ahci,id=ahci -drive id=disk,file=%s,if=none,format=raw,cache=writeback "
         "-device ide-hd,drive=disk,bus=ahci.0") % img) if os.environ.get('AHCI')
       else ("-drive file=%s,format=raw,if=ide" % img), iso, os.environ.get('QEMU_EXTRA', '')))
p = pexpect.spawn(cmd, encoding='utf-8', codec_errors='replace', timeout=3600)
p.logfile_read = log
i = p.expect(['login:', 'Give root password'])
if i == 1:
    p.sendline('hurd')
    p.expect('#', timeout=600)
    p.sendline('fsck -fy /dev/wd0s1; sync; echo FSCKDONE')
    p.expect('FSCKDONE', timeout=3600)
    p.sendline('reboot')
    try:
        p.expect(pexpect.EOF, timeout=600)
    except Exception:
        pass
    print("repaired filesystem, rerun", flush=True)
    sys.exit(3)
p.sendline('root')
i = p.expect(['assword:', '#'], timeout=300)
if i == 0:
    p.sendline('hurd')
    if p.expect(['#', 'Invalid password'], timeout=300) == 1:
        p.expect('login:', timeout=300)
        p.sendline('root')
        p.expect('assword:', timeout=300)
        p.sendline('')
        p.expect('#', timeout=300)
time.sleep(3)
for c in ["echo root:hurd | chpasswd",
          "sed -i 's/^#*PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config",
          "grep -q '^PermitRootLogin yes' /etc/ssh/sshd_config || echo 'PermitRootLogin yes' >> /etc/ssh/sshd_config",
          "/etc/init.d/ssh restart; echo SSHREADY"]:
    p.sendline(c)
    time.sleep(2)
p.expect('SSHREADY', timeout=600)
print("dev VM ready, ssh on port 2222", flush=True)
open(os.environ.get('READY', '/hb/DEVVM_READY'), 'w').write('1')
p.expect(pexpect.EOF, timeout=None)

Debian GNU/Hurd x86_64 on several processors - mach-SMP release smp-v1.0
https://github.com/MevPR/mach-SMP/releases/tag/smp-v1.0

Login: root, password "hurd" (change it: passwd).  sshd allows root
logins with password; do not expose port 22 of this VM to a network.

Boots /boot/gnumach-1.8-amd64-smp-v1.0.gz with smp_pset=default (all
CPUs in the default processor set).  The stock uniprocessor kernel is
still in the GRUB menu under "Advanced options".

Installed fixes (originals kept in /root/orig):
  hurd-SMP: libports, libfshelp, libpager, ext2fs(.static), pflocal,
            console (/root/orig/hfix)
  netdde:   libddekit timer fix, binary patch (/root/orig/hfix/netdde)
  glibc:    fork fix, binary patch of libc.so.0.3 (/root/orig)

Chromium test (as for the release):
  sh /root/chrome-test.sh 1800 run1
  -> screenshots /tmp/shot-run1-*.png (DISPLAY :1 via Xvfb)
Under QEMU without KVM the page appears after 15-20 minutes.

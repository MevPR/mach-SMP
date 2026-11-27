#!/bin/sh
# Install the hurd-SMP fix build; originals are kept in /root/orig/hfix.
set -e
cd /root/hurd-0.9.git20261028/build
mkdir -p /root/orig/hfix
for l in libports libfshelp libpager; do
  f=/usr/lib/x86_64-gnu/$l.so.0.3
  [ -f /root/orig/hfix/$l.so.0.3 ] || cp -p $f /root/orig/hfix/
  install -m 644 $l/$l.so.0.3 $f.new && mv $f.new $f
done
inst() { [ -f /root/orig/hfix/$(basename $2) ] || cp -p $2 /root/orig/hfix/; install -m 755 $1 $2.new && mv $2.new $2; }
inst pflocal/pflocal /hurd/pflocal
inst ext2fs/ext2fs.static /hurd/ext2fs.static
inst ext2fs/ext2fs /hurd/ext2fs
inst console-client/console /usr/bin/console
sync
ls -la /root/orig/hfix
echo INSTALL-OK

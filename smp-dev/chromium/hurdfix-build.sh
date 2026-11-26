#!/bin/sh
# In the Chromium VM: build the Debian hurd package sources with the
# MevPR/hurd-SMP smp-fixes patches (in /root/hfix) and install the
# affected libraries and servers; originals are kept in /root/orig.
set -e
cd /root
echo "deb-src [trusted=yes] http://deb.debian.org/debian sid main" > /etc/apt/sources.list.d/src.list
apt-get update -qq >/dev/null 2>&1 || true
V=$(dpkg-query -W -f='${Version}' hurd | sed 's/^[0-9]*://; s/-[^-]*$//')
echo "hurd $V"
[ -d hurd-$V ] || apt-get source -qq hurd=$(dpkg-query -W -f='${Version}' hurd) >/root/src.log 2>&1
apt-get build-dep -y -qq hurd >/root/bdep.log 2>&1
cd hurd-$V
[ -f .hfix-applied ] || for p in /root/hfix/*.patch; do
  if patch -p1 -R --dry-run -s -f < $p >/dev/null 2>&1; then echo "already: $(basename $p)"
  elif patch -p1 --dry-run -s -f < $p >/dev/null 2>&1; then patch -p1 -s < $p; echo "applied: $(basename $p)"
  else echo "FAILED: $(basename $p)"; exit 1; fi
done
touch .hfix-applied
[ -f configure ] || autoreconf -fi >/root/autoreconf.log 2>&1
mkdir -p build && cd build
[ -f Makefile ] || ../configure --prefix=/usr --libdir=/usr/lib/x86_64-gnu --disable-profile --without-parted --without-rump MIG=x86_64-gnu-mig >/root/configure.log 2>&1
make -j2 libports libfshelp libpager pflocal console-client ext2fs >/root/hfix-build.log 2>&1 || { tail -20 /root/hfix-build.log; exit 1; }
make -C ext2fs ext2fs.static >>/root/hfix-build.log 2>&1
mkdir -p /root/orig/hfix
for l in libports libfshelp libpager; do
  f=/usr/lib/x86_64-gnu/$l.so.0.3; [ -f /root/orig/hfix/$l.so.0.3 ] || cp -p $f /root/orig/hfix/
  install -m 644 $l/$l.so.0.3 $f.new && mv $f.new $f
done
for f in /hurd/pflocal /hurd/ext2fs.static /hurd/ext2fs /usr/bin/console; do
  [ -f /root/orig/hfix/$(basename $f) ] || cp -p $f /root/orig/hfix/
done
install -m 755 pflocal/pflocal /hurd/pflocal.new && mv /hurd/pflocal.new /hurd/pflocal
install -m 755 ext2fs/ext2fs.static /hurd/ext2fs.static.new && mv /hurd/ext2fs.static.new /hurd/ext2fs.static
install -m 755 ext2fs/ext2fs /hurd/ext2fs.new && mv /hurd/ext2fs.new /hurd/ext2fs
install -m 755 console-client/console /usr/bin/console.new && mv /usr/bin/console.new /usr/bin/console
sync
echo HURDFIX-OK

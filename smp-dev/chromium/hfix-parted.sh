#!/bin/sh
# Rebuild libstore with parted support (needed for part:N:device:wd0)
# and relink ext2fs.static; install only if it has the part store.
set -e
cd /root/hurd-0.9.git20261028/build
dpkg -s libparted-dev >/dev/null 2>&1 || apt-get install -y -qq libparted-dev >/dev/null 2>&1
../configure --prefix=/usr --libdir=/usr/lib/x86_64-gnu --disable-profile --without-rump MIG=x86_64-gnu-mig >/root/configure2.log 2>&1
grep -i "parted" config.make | head -3
rm -f libstore/*.o libstore/*.a libstore/*.so* ext2fs/ext2fs.static
make -C libstore >/root/hfix-parted.log 2>&1 || { tail -15 /root/hfix-parted.log; exit 1; }
make -C ext2fs ext2fs.static >>/root/hfix-parted.log 2>&1 || { tail -15 /root/hfix-parted.log; exit 1; }
n=$(nm ext2fs/ext2fs.static | grep -c "store_part_class\|ped_disk_new")
echo "part symbols: $n"
[ "$n" -ge 2 ]
install -m 755 ext2fs/ext2fs.static /hurd/ext2fs.static.new && mv /hurd/ext2fs.static.new /hurd/ext2fs.static
sync
echo PARTED-OK

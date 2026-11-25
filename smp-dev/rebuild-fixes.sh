#!/bin/sh
# Run inside the dev VM: apply rlock + libpager fixes, rebuild ext2fs (normal + dbgmalloc) and libpager.so.
set -e
H=/root/hurd-0.9.git20250801
cd $H
cp /root/rlockfix/* libfshelp/
for p in pager; do
  if patch -p1 -R --dry-run -s -f < /root/$p.patch >/dev/null 2>&1; then echo "$p already applied"
  else patch -p1 -s < /root/$p.patch; echo "$p applied"; fi
done
cd build
make -C libfshelp > /root/fix-build.log 2>&1
make -C libpager >> /root/fix-build.log 2>&1
rm -f ext2fs/ext2fs.static
make -C ext2fs ext2fs.static >> /root/fix-build.log 2>&1
cp ext2fs/ext2fs.static /root/ext2fs.static.fix4
rm -f ext2fs/ext2fs.static
make -C ext2fs ext2fs.static LDFLAGS="/root/dbgmalloc.o -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc" >> /root/fix-build.log 2>&1
mv ext2fs/ext2fs.static ext2fs/ext2fs-dbg.static
cp /root/ext2fs.static.fix4 ext2fs/ext2fs.static
nm ext2fs/ext2fs-dbg.static | grep -c __wrap_malloc
install -m 755 ext2fs/ext2fs-dbg.static /hurd/ext2fs.static
ls libpager/libpager.so* 2>/dev/null
sync
echo REBUILD-OK

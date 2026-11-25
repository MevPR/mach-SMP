#!/bin/sh
set -e
cd /root/hurd-0.9.git20250801/build
gcc -O2 -g -c /root/dbgmalloc.c -o /root/dbgmalloc.o
cp ext2fs/ext2fs.static /root/ext2fs.static.keep
rm -f ext2fs/ext2fs.static
make -C ext2fs ext2fs.static LDFLAGS="/root/dbgmalloc.o -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc" > /root/dbglink.log 2>&1
mv ext2fs/ext2fs.static ext2fs/ext2fs-dbg.static
cp /root/ext2fs.static.keep ext2fs/ext2fs.static
nm ext2fs/ext2fs-dbg.static | grep -c dbgmalloc_last
install -m 755 ext2fs/ext2fs-dbg.static /hurd/ext2fs.static
sync
echo DBG-OK

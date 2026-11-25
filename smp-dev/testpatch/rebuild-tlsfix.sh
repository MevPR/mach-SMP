#!/bin/sh
# In the dev VM: relink ext2fs.static (normal and dbgmalloc) with the TLS fix.
set -e
cd /root/hurd-0.9.git20250801/build
gcc -O2 -g -c /root/tlsfix.c -o /root/tlsfix.o
gcc -O2 -g -c /root/dbgmalloc.c -o /root/dbgmalloc.o
rm -f ext2fs/ext2fs.static
make -C ext2fs ext2fs.static LDFLAGS="/root/tlsfix.o -Wl,--wrap=_dl_allocate_tls" > /root/tlslink.log 2>&1
cp ext2fs/ext2fs.static /root/ext2fs.static.tlsfix
rm -f ext2fs/ext2fs.static
make -C ext2fs ext2fs.static LDFLAGS="/root/tlsfix.o /root/dbgmalloc.o -Wl,--wrap=_dl_allocate_tls,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc" >> /root/tlslink.log 2>&1
mv ext2fs/ext2fs.static ext2fs/ext2fs-dbg.static
cp /root/ext2fs.static.tlsfix ext2fs/ext2fs.static
nm ext2fs/ext2fs.static | grep -c __wrap__dl_allocate_tls
nm ext2fs/ext2fs-dbg.static | grep -c "__wrap__dl_allocate_tls\|__wrap_malloc"
install -m 755 ext2fs/ext2fs-dbg.static /hurd/ext2fs.static
sync
echo TLSFIX-OK

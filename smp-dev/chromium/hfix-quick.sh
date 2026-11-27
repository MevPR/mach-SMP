#!/bin/sh
# Finish the hurd-SMP fix build quickly: only the patched libraries are
# built, the other libraries come from the installed hurd-dev.
set -e
pkill -f build-hurdfix.sh || true; pkill make || true; sleep 3
cd /root/hurd-0.9.git20261028/build
make -C libports >/root/hfix-quick.log 2>&1
make -C libfshelp >>/root/hfix-quick.log 2>&1
make -C libpager >>/root/hfix-quick.log 2>&1
for l in diskfs iohelp store ihash shouldbeinlibc trivfs netfs pipe bpf ps hurdbugaddr; do
  mkdir -p lib$l
  for x in a so; do
    [ -e lib$l/lib$l.$x ] && [ ! -L lib$l/lib$l.$x ] && continue
    ln -sf /usr/lib/x86_64-gnu/lib$l.$x lib$l/lib$l.$x
  done
done
make -C ext2fs ext2fs ext2fs.static >>/root/hfix-quick.log 2>&1 || { tail -15 /root/hfix-quick.log; exit 1; }
make -C pflocal >>/root/hfix-quick.log 2>&1 || { tail -15 /root/hfix-quick.log; exit 1; }
make -C console-client console >>/root/hfix-quick.log 2>&1 || { tail -15 /root/hfix-quick.log; exit 1; }
ls -la libports/libports.so.0.3 libfshelp/libfshelp.so.0.3 libpager/libpager.so.0.3 ext2fs/ext2fs ext2fs/ext2fs.static pflocal/pflocal console-client/console
echo QUICK-BUILD-OK

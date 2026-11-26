#!/bin/sh
# In the dev VM: apply the libddekit timer and libmachdevdde double-reply fixes,
# rebuild libddekit and libmachdevdde, install libmachdevdde.so.0.3.
set -e
cd /root/hurd-0.9.git20250801
for p in ddekit-timer mdde-write; do
  if patch -p1 -R --dry-run -s -f < /root/$p.patch >/dev/null 2>&1; then echo "$p already applied"; else patch -p1 -s < /root/$p.patch; echo "$p applied"; fi
done
cd build
make libddekit libmachdevdde > /root/mdde-build.log 2>&1 || { tail -15 /root/mdde-build.log; exit 1; }
ls -la libmachdevdde/libmachdevdde.so* libddekit/libddekit* 2>/dev/null | head
L=/usr/lib/x86_64-gnu/libmachdevdde.so.0.3
mkdir -p /root/orig; [ -f /root/orig/libmachdevdde.so.0.3.binpatched ] || cp $L /root/orig/libmachdevdde.so.0.3.binpatched
install -m 644 libmachdevdde/libmachdevdde.so.0.3 $L
nm -D $L | grep -c " T " ; sync
echo MDDE-OK

#!/bin/sh
# In the dev VM: relink the static boot servers with the multiple_threads TLS fix.
set -e
cd /root/hurd-0.9.git20250801/build
gcc -O2 -g -c /root/tlsfix.c -o /root/tlsfix.o
mkdir -p /root/orig
for d in pci-arbiter acpi rumpdisk; do
  [ -f /root/orig/$d.static ] || cp /hurd/$d.static /root/orig/$d.static
  make $d > /root/static-dep-$d.log 2>&1 || { echo "$d deps FAILED"; tail -5 /root/static-dep-$d.log; exit 1; }
  rm -f $d/$d.static
  make -C $d $d.static LDFLAGS="/root/tlsfix.o -Wl,--wrap=_dl_allocate_tls" > /root/static-$d.log 2>&1 || { echo "$d FAILED"; tail -5 /root/static-$d.log; exit 1; }
  echo "$d: $(nm $d/$d.static | grep -c __wrap__dl_allocate_tls) wrap, $(ls -la $d/$d.static | awk '{print $5}') bytes"
  install -m 755 $d/$d.static /hurd/$d.static
done
sync
echo STATICS-OK

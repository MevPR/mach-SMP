#!/bin/sh
# In the dev VM: apply the console-client SIGPIPE fix, rebuild and install /usr/bin/console.
set -e
cd /root/hurd-0.9.git20250801
if patch -p1 -R --dry-run -s -f < /root/console.patch >/dev/null 2>&1; then echo "already applied"; else patch -p1 -s < /root/console.patch; echo applied; fi
cd build
rm -f console-client/driver.o console-client/console
make -C console-client console libdir=/usr/lib/x86_64-gnu > /root/console-build.log 2>&1 || { tail -15 /root/console-build.log; exit 1; }
mkdir -p /root/orig; [ -f /root/orig/console ] || cp /usr/bin/console /root/orig/console
install -m 755 console-client/console /usr/bin/console
grep -c send_retval ../console-client/console.c; strings console-client/console | grep "hurd/console"; ls -la /usr/bin/console; sync
echo CONSOLE-OK

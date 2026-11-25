#!/bin/sh
# In the dev VM: apply the pflocal unbind fix to the Debian hurd source, rebuild and install pflocal.
set -e
cd /root/hurd-0.9.git20250801
if patch -p1 -R --dry-run -s -f < /root/pflocal.patch >/dev/null 2>&1; then echo "already applied"; else patch -p1 -s < /root/pflocal.patch; echo applied; fi
cd build
make pflocal > /root/pflocal-build.log 2>&1 || { tail -15 /root/pflocal-build.log; exit 1; }
mkdir -p /root/orig; [ -f /root/orig/pflocal ] || cp /hurd/pflocal /root/orig/pflocal
install -m 755 pflocal/pflocal /hurd/pflocal
ls -la /hurd/pflocal; sync
echo PFLOCAL-OK

#!/bin/sh
# Run hfix-quick.sh; when a MIG header of a library not built here is
# missing, generate just that header, drop the dependency files that
# recorded it as missing (gcc -MG), and try again.
cd /root/hurd-0.9.git20261028/build
for i in $(seq 1 30); do
  sh /root/hfix-quick.sh > /root/hfix-quick.out 2>&1 && { tail -3 /root/hfix-quick.out; exit 0; }
  h=$(grep -o "No rule to make target '[^']*_[SU]\.h'" /root/hfix-quick.out | head -1 | sed "s/.*'\(.*\)'/\1/")
  [ -n "$h" ] || { echo "OTHER FAILURE"; tail -20 /root/hfix-quick.out; exit 1; }
  h=${h#../}
  if [ ! -f $h ]; then
    echo "generating $h"
    make -C $(dirname $h) $(basename $h) >/dev/null 2>&1 || { echo "cannot make $h"; exit 1; }
  fi
  echo "dropping stale deps for $h"
  grep -l "[ :]$h" ext2fs/*.d pflocal/*.d console-client/*.d 2>/dev/null | xargs rm -f
done

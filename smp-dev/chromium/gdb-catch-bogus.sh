#!/bin/sh
# In the VM container: like gdb-catch-assert.sh, plus a core of every task
# that deallocates a bogus port (catchbogus.py).
K=$1; OUT=$2
gdb -q -batch -ex "set pagination off" -ex "file $K" -ex "target remote :1236" \
  -ex "source /hb/catchbogus.py" \
  -ex "break Assert" -ex "break Panic" -ex "continue" \
  -ex "info threads" -ex "bt 30" -ex "thread apply all bt 25" > $OUT 2>&1
echo "gdb finished" >> $OUT

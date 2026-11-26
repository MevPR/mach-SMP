#!/bin/sh
# In the VM container: wait on the gdbstub until Assert or Panic is hit, then dump state.
K=$1; OUT=$2
gdb -q -batch -ex "set pagination off" -ex "file $K" -ex "target remote :1236" \
  -ex "break Assert" -ex "break Panic" -ex "continue" \
  -ex "info threads" -ex "bt 30" -ex "thread apply all bt 25" \
  -ex "p *page" -ex "frame 1" -ex "info locals" > $OUT 2>&1
echo "gdb finished" >> $OUT

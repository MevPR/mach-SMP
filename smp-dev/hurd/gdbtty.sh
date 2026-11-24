# usage: gdbtty.sh <iso> <smp> <log> <seconds> <kernel-elf>
apt-get install -y -qq gdb >/dev/null 2>&1 || (apt-get update -qq >/dev/null && apt-get install -y -qq gdb >/dev/null 2>&1)
cd /img && rm -f overlay-$$.qcow2 && qemu-img create -q -f qcow2 -F raw -b debian-hurd-amd64-20250807.img overlay-$$.qcow2
qemu-system-x86_64 -m 2048 -smp $2 -cpu core2duo-v1 -nographic -no-reboot -monitor none -gdb tcp::1235 \
  -drive file=/img/overlay-$$.qcow2,format=qcow2,if=ide -cdrom $1 -boot d -serial file:$3 >/dev/null 2>&1 &
Q=$!
sleep $4
tail -c 200 $3 | tr -d '\r'; echo
gdb -q -batch -ex "set pagination off" -ex "file $5" -ex "target remote :1235" \
  -ex "print/x com_tty[0].t_state" -ex "print com_tty[0].t_outq.c_cc" -ex "print com_tty[0].t_inq.c_cc" \
  -ex "print comtimer_state" -ex "print/x com_tty[0].t_lock" -ex "print comst_1" -ex "print comst_3" 2>&1 | grep -v "^warning"
kill $Q; rm -f /img/overlay-$$.qcow2

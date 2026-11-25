# usage: gdbboot.sh <iso> <smp> <log> <seconds-before-gdb> <kernel-elf>
apt-get install -y -qq gdb >/dev/null 2>&1 || (apt-get update -qq >/dev/null && apt-get install -y -qq gdb >/dev/null 2>&1)
cd /img && rm -f overlay.qcow2 && qemu-img create -q -f qcow2 -F raw -b debian-hurd-amd64-20250807.img overlay.qcow2
qemu-system-x86_64 -m 2048 -smp $2 -cpu core2duo-v1 -nographic -no-reboot -monitor none -s \
  -drive file=/img/overlay.qcow2,format=qcow2,if=ide -cdrom $1 -boot d -serial file:$3 >/dev/null 2>&1 &
Q=$!
sleep $4
tail -c 300 $3 | tr -d '\r'; echo
gdb -q -batch -ex "set pagination off" -ex "file $5" -ex "target remote :1234" \
  -ex "info threads" -ex "thread apply all bt 25" 2>&1 | grep -v "^warning"
kill $Q

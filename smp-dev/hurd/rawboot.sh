# usage: rawboot.sh <iso> <smp> <log> <seconds>
cd /img && rm -f overlay.qcow2 && qemu-img create -q -f qcow2 -F raw -b debian-hurd-amd64-20250807.img overlay.qcow2
timeout $4 qemu-system-x86_64 -m 2048 -smp $2 -cpu core2duo-v1 -nographic -no-reboot -monitor none \
  -drive file=/img/overlay.qcow2,format=qcow2,if=ide -cdrom $1 -boot d -serial file:$3 >/dev/null 2>&1
exit 0

# usage: series.sh <iso> <tag> <runs> <seconds>
for r in $(seq 1 $3); do
  O=/img/ov-$2-$r.qcow2; L=/hb/series-$2-$r.log
  qemu-img create -q -f qcow2 -F raw -b /img/${BASE:-debian-hurd-amd64-20250807.img} $O
  timeout $4 qemu-system-x86_64 -m 2048 -smp 2 -cpu core2duo-v1 -nographic -no-reboot -monitor none \
    -drive file=$O,format=qcow2,if=ide -cdrom $1 -boot d -serial file:$L >/dev/null 2>&1
  rm -f $O
  if grep -aq "login:" $L; then echo "$2 run $r: login"; else echo "$2 run $r: STUCK after: $(tr -d '\r' < $L | grep -av '^$' | tail -1)"; fi
done

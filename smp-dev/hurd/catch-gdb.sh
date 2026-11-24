# usage: catch-gdb.sh <iso> <kernel-elf> <maxruns>
command -v gdb >/dev/null || { apt-get update -qq >/dev/null; apt-get install -y -qq gdb >/dev/null 2>&1; }
for r in $(seq 1 $3); do
  O=/img/ov-gdb.qcow2; L=/hb/cg-$r.log; rm -f $L
  qemu-img create -q -f qcow2 -F raw -b /img/debian-hurd-amd64-20250807.img $O
  qemu-system-x86_64 -m 2048 -smp 2 -cpu core2duo-v1 -nographic -no-reboot -monitor none -gdb tcp::1236 \
    -drive file=$O,format=qcow2,if=ide -cdrom $1 -boot d -serial file:$L >/dev/null 2>&1 &
  Q=$!; last=0; still=0; hung=0
  for t in $(seq 1 180); do
    sleep 5
    if grep -aq "login:" $L 2>/dev/null; then break; fi
    s=$(stat -c %s $L 2>/dev/null || echo 0)
    if [ "$s" = "$last" ]; then still=$((still+5)); else still=0; last=$s; fi
    if [ $still -ge 150 ]; then hung=1; break; fi
  done
  if [ $hung = 1 ]; then
    echo "run $r: HUNG after: $(tr -d '\r' < $L | grep -av '^$' | tail -1)"
    gdb -q -batch -ex "set pagination off" -ex "file $2" -ex "target remote :1236" -ex "source /hb/ipcwait.py" 2>&1 | grep -v "^warning" > /hb/cg-$r-ipc.txt
    tail -3 /hb/cg-$r-ipc.txt
    echo "VM left running for inspection (gdb on :1236)"; touch /hb/HUNG
    wait $Q; exit 0
  fi
  echo "run $r: $(grep -aq login: $L && echo login || echo timeout)"
  kill $Q; wait $Q 2>/dev/null; rm -f $O
done

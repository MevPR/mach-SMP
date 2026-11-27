#!/bin/sh
# usage: bootloop.sh <runs> <resultfile>   (on the host, in the mach-smp dir)
# Boot images/chromium.raw on the SMP kernel N times; record whether DHCP
# completed (READY) or hung.
N=$1; R=$2; cd /Volumes/EXTTB2/Xcode/mach-smp
for i in $(seq 1 $N); do
  docker rm -f chrvm >/dev/null 2>&1
  docker run --rm --platform linux/amd64 -v "$PWD/images":/img hurd-smp-build sh -c 'e2fsck -fy "/img/chromium.raw?offset=1048576" >/dev/null 2>&1'
  rm -f hurdboot/CHRVM_READY
  n=$(wc -l < hurdboot/chrvm-serial.log)
  docker run -d --name chrvm --platform linux/amd64 -v "$PWD/hurdboot":/hb -v "$PWD/images":/img -v "$PWD/build":/out \
    -e SMP=2 -e MEM=4096 -e LOG=/hb/chrvm-serial.log -e READY=/hb/CHRVM_READY -e AHCI=1 -e IMG=/img/chromium.raw \
    hurd-smp-build sh -c 'apt-get update -qq >/dev/null; apt-get install -y -qq sshpass openssh-client >/dev/null 2>&1; python3 /hb/devvm.py /hb/smp-int3.iso' >/dev/null
  t0=$(date +%s); td=""; res=TIMEOUT
  while [ $(( $(date +%s) - t0 )) -lt 1800 ]; do
    [ -f hurdboot/CHRVM_READY ] && { res=READY; break; }
    L=$(tail -n +$n hurdboot/chrvm-serial.log)
    echo "$L" | grep -aq "panic\|Assert" && { res=PANIC; break; }
    if echo "$L" | grep -aq DHCPREQUEST; then
      [ -z "$td" ] && td=$(date +%s)
      echo "$L" | grep -aq "DHCPACK\|bound to" || { [ $(( $(date +%s) - td )) -gt 600 ] && { res=DHCP-HANG; break; }; }
    fi
    sleep 15
  done
  echo "run $i: $res after $(( $(date +%s) - t0 ))s" | tee -a $R
  if [ $res = READY ]; then
    docker exec chrvm timeout 60 sh /hb/vmssh.sh 'sync; (sleep 2; halt) >/dev/null 2>&1 &' >/dev/null 2>&1; sleep 90
  fi
done
echo BOOTLOOP-DONE | tee -a $R

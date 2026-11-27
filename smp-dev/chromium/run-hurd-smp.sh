#!/bin/sh
# Start the mach-SMP smp-v1.0 Debian GNU/Hurd image under QEMU.
#   sh run-hurd-smp.sh                  4 CPUs, 8 GB, window
#   CPUS=2 MEM=4G sh run-hurd-smp.sh
#   HEADLESS=1 sh run-hurd-smp.sh       no window; log in with ssh
# ssh: ssh -p 2222 root@localhost   (password "hurd"; change it)
# Needs QEMU >= 5.1 (zstd-compressed qcow2).
IMG=${IMG:-hurd-smp-v1.0.qcow2}
CPUS=${CPUS:-4}
MEM=${MEM:-8G}
SSH_PORT=${SSH_PORT:-2222}
if [ -w /dev/kvm ] && [ "$(uname -m)" = x86_64 ]; then
  ACCEL="-accel kvm -cpu host"
else
  ACCEL="-accel tcg,thread=multi -cpu core2duo-v1"   # emulation: slow
fi
[ -n "$HEADLESS" ] && DISPLAY_OPT="-display none" || DISPLAY_OPT=""
exec qemu-system-x86_64 -m "$MEM" -smp "$CPUS" $ACCEL $DISPLAY_OPT \
  -device ahci,id=ahci \
  -drive id=disk,file="$IMG",if=none,format=qcow2,cache=writeback \
  -device ide-hd,drive=disk,bus=ahci.0 \
  -nic user,model=e1000,hostfwd=tcp::"$SSH_PORT"-:22 \
  -serial file:hurd-serial.log

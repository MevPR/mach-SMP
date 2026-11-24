# usage: explore.sh <builddir-name> <smp> <runs> [configure args]
set -e
NAME=$1; SMP=$2; RUNS=$3; shift 3
(cd /src && autoreconf -fi >/dev/null 2>&1)
B=/out/explore/$NAME; mkdir -p $B; cd $B
[ -f Makefile ] || /src/configure --prefix= --host=x86_64-gnu MIG=x86_64-gnu-mig LD=x86_64-linux-gnu-ld CC=x86_64-linux-gnu-gcc USER_MIG=x86_64-gnu-mig USER_CC=x86_64-linux-gnu-gcc USER_LD=x86_64-linux-gnu-ld USER_CPP='x86_64-linux-gnu-gcc -E' --disable-linux-groups --enable-apic --enable-ncpus=8 --enable-kdb "$@" >/dev/null
make -j$(nproc) gnumach.gz >/dev/null 2>&1 || { make gnumach.gz 2>&1 | grep -i error | head; exit 1; }
rm -f tests/test-smp tests/test-smp.iso tests/module-smp
make tests/test-smp.iso tests/test-smp QEMU_OPTS="-m 2047 -nographic -no-reboot -monitor none -serial stdio -boot d -cpu core2duo-v1 -smp $SMP" >/dev/null 2>&1
for r in $(seq 1 $RUNS); do
  if timeout 300 sh tests/test-smp > run-$r.log 2>&1; then res=PASS; else res=FAIL; fi
  echo "run $r: $res $(grep -a 'workers ran\|^panic\|test-failure' run-$r.log | head -2 | tr '\n' ' ')"
done

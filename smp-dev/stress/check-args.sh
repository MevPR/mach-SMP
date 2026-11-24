# usage: check-args.sh <name> <smp> "<gnumach args>" [configure args]
set -e
NAME=$1; SMP=$2; ARGS=$3; shift 3
(cd /src && autoreconf -fi >/dev/null 2>&1)
B=/out/check/$NAME; mkdir -p $B; cd $B
[ -f Makefile ] || /src/configure --prefix= --host=x86_64-gnu MIG=x86_64-gnu-mig LD=x86_64-linux-gnu-ld CC=x86_64-linux-gnu-gcc USER_MIG=x86_64-gnu-mig USER_CC=x86_64-linux-gnu-gcc USER_LD=x86_64-linux-gnu-ld USER_CPP='x86_64-linux-gnu-gcc -E' --disable-linux-groups --enable-apic --enable-ncpus=8 --enable-kdb "$@" >/dev/null
make -j$(nproc) gnumach.gz >/dev/null 2>&1
rm -f tests/test-*[!.c] 2>/dev/null || true
make -k check GNUMACH_ARGS="$ARGS" QEMU_OPTS="-m 2047 -nographic -no-reboot -monitor none -serial stdio -boot d -cpu core2duo-v1 -smp $SMP" > check.log 2>&1 || true
grep -E "^(PASS|FAIL|ERROR|XFAIL|SKIP):" check.log | tr '\n' ' '; echo

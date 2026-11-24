#!/bin/sh
# Build GNU Mach for x86_64 with SMP in a Debian amd64 container and run
# the QEMU test suite.
#
# Usage: smp-dev/build.sh [ncpus] [extra configure args...]
# Output: $OUT/<srcdir-name>-x86_64-smp<ncpus>/ (outside the source tree)
set -e
NCPUS=${1:-8}
[ $# -gt 0 ] && shift
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/../build}
mkdir -p "$OUT"
IMAGE=hurd-smp-build
docker image inspect $IMAGE >/dev/null 2>&1 ||
  docker build --platform linux/amd64 -t $IMAGE "$SRC/smp-dev"
exec docker run --rm --platform linux/amd64 \
  -v "$SRC":/src -v "$(cd "$OUT" && pwd)":/out -w /src $IMAGE sh -ec "
  [ -x configure ] || autoreconf -fi
  B=/out/$(basename "$SRC")-x86_64-smp$NCPUS
  mkdir -p \$B && cd \$B
  [ -f Makefile ] || /src/configure --prefix= --host=x86_64-gnu \
     MIG=x86_64-gnu-mig LD=x86_64-linux-gnu-ld CC=x86_64-linux-gnu-gcc \
     USER_MIG=x86_64-gnu-mig USER_CC=x86_64-linux-gnu-gcc \
     USER_LD=x86_64-linux-gnu-ld USER_CPP='x86_64-linux-gnu-gcc -E' \
     --disable-linux-groups --enable-apic --enable-ncpus=$NCPUS --enable-kdb $*
  make -j\$(nproc) gnumach.gz
  make VERBOSE=true check
"

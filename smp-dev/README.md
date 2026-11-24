# smp-dev: SMP work on GNU Mach (x86_64)

This branch (`main`) is upstream GNU Mach (`upstream` branch, mirrored from
git.savannah.gnu.org/git/hurd/gnumach.git) plus this directory.

Patches meant for bug-hurd@gnu.org are developed on topic branches forked
from `upstream`, so `git format-patch upstream` produces clean series.

## Build and test

Requirements: Docker (on Apple Silicon, amd64 emulation via Rosetta).

    smp-dev/build.sh            # NCPUS=8, APIC, kdb; runs `make check` (QEMU -smp 2)
    smp-dev/build.sh 4 --disable-kdb

The container is Debian sid amd64 with `mig-x86-64-gnu`, the x86_64 GCC
toolchain, QEMU and grub-mkrescue. Builds land in `../build/` (override
with `OUT=`).

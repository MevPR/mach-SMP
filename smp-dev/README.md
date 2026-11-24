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

## Stress runs (`smp-dev/stress/`)

Run inside the container with the source tree at `/src` and an output
directory at `/out`:

    docker run --rm --platform linux/amd64 -v $PWD:/src -v $PWD/../build:/out \
      hurd-smp-build sh /src/smp-dev/stress/explore.sh <name> <smp> <runs> [configure args]
    # repeat tests/test-smp under qemu -smp <smp>
    docker run ... sh /src/smp-dev/stress/check-args.sh <name> <smp> "console=com0 smp_pset=default" [configure args]
    # whole `make check` with extra kernel arguments

## Booting Debian GNU/Hurd amd64 (`smp-dev/hurd/`)

Mounts: `/src` gnumach tree, `/out` build output, `/hb` this directory
(or a copy), `/img` a directory holding
`debian-hurd-amd64-20250807.img` from
cdimage.debian.org/cdimage/ports/latest/hurd-amd64/.  The image is
never modified: every boot uses a fresh qcow2 overlay.

    sh /hb/build-kernel.sh integration          # -> /out/kernel/integration/gnumach.gz
    sh /hb/mkiso.sh /out/kernel/integration/gnumach.gz /hb/smp-all.iso smp_pset=default
    sh /hb/rawboot.sh /hb/smp-all.iso 2 /hb/boot.log 600     # serial log only
    python3 /hb/drive.py /hb/smp-all.iso 2 /hb/drive.log /hb/cmds-load.txt
    sh /hb/gdbboot.sh /hb/smp-all.iso 2 /hb/boot.log 600 /out/kernel/integration/gnumach

Under Rosetta, 4 emulated CPUs take well over 10 minutes to reach the
login prompt; 2 are usable.

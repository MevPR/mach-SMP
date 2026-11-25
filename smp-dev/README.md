# smp-dev: SMP work on GNU Mach (x86_64)

This branch (`main`) is upstream GNU Mach (`upstream` branch, mirrored from
git.savannah.gnu.org/git/hurd/gnumach.git) plus this directory.

Patches meant for bug-hurd@gnu.org are developed on topic branches forked
from `upstream`, so `git format-patch upstream` produces clean series.
See the top-level [README.md](../README.md) for what was found and fixed.

## Branches

| Branch | Content |
|---|---|
| `upstream` | mirror of git.savannah.gnu.org/git/hurd/gnumach.git |
| `main` | `upstream` + README.md + this directory |
| `submit-smp` | the 17-patch gnumach series as prepared for bug-hurd, linear on upstream master |
| `percpu-volatile`, `fsgs-base-check`, `panic-lock`, `com-tty-lock`, `pset-empty-release`, `smp-lock-debug`, `smp-stop-ipi`, `smp-default-pset` | one topic each, forked from `upstream` |
| `smp-tests` | `smp-default-pset` + the new SMP tests |
| `smp-integration` | all topics merged: the kernel used for the Hurd boots and stress runs |

## Layout

| Path | Content |
|---|---|
| `notes/hang-fork-suspend.md` | the investigation log: every hang and crash, how it was caught, root cause, test results |
| `INVENTORY.md` | initial inventory of the SMP code and the agreed work order |
| `outbox/` | patch series ready for upstream (gnumach, hurd, netdde, rumpkernel, glibc) and `send.sh` (dry run by default, `--send` to mail them) |
| `patches-hurd/` | the userland patches as single files (glibc, hurd, netdde, rumpkernel), including the libports variant adapted to Debian's hurd 0.9.git20250801 |
| `testpatch/` | how the userland fixes were applied to the test image: binary patches (`binpatch.py`) and rebuild scripts for the Hurd VM |
| `stress/` | kernel stress scripts, and userland test programs for a running Hurd: `mtstress.c` (multithreaded malloc torture), `heapsim.c` (lock plus brk-style heap without glibc malloc), `tlsfix.c` (link-time workaround for the glibc `multiple_threads` bug in static binaries) |
| `dbgmalloc.c` | quarantining malloc for Hurd servers (`-Wl,--wrap=malloc,...`): catches writes after free and double frees, keeps the last report in `dbgmalloc_last` for cores |
| `hurd/` | booting and driving Debian GNU/Hurd amd64 under QEMU, and the gdb scripts used to analyse a hung system from the kernel (see below) |
| `build.sh`, `Dockerfile` | the build container |

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

## Analysing a hung Hurd from the kernel (`smp-dev/hurd/`)

Boot with QEMU's gdb stub (`QEMU_EXTRA="-gdb tcp::1236"` for `drive.py`,
or `catch-gdb.sh`, which leaves a hung VM running), attach gdb with the
kernel image, then `source` one of:

| Script | Shows |
|---|---|
| `ipcwait.py` | every thread blocked in `mach_msg`, with the port it waits on and queued messages |
| `otherwait.py` | threads blocked anywhere else (kernel waits, exceptions) |
| `susp.py`, `susphist.py`, `dostop.py` | suspend counts and who stopped whom |
| `kstacks.py` | kernel stacks of the threads of given tasks |
| `mkcore.py` | an ELF core of one task (`CORE_TASK`, `CORE_OUT`), readable by gdb with the task's binary (`set osabi GNU/Linux`) |
| `uread.py`, `libmap.py`, `findbase.py` | user memory reads, shared library bases from the page hash |

`corefull.sh` symbolizes a core with the Debian debug symbols
(snapshot.debian.org, debian-ports 20250807).

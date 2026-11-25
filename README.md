# mach-SMP: making Debian GNU/Hurd amd64 work on several processors

This is a fork of [GNU Mach](https://www.gnu.org/software/hurd/microkernel/mach/gnumach.html)
(the microkernel of the GNU Hurd) for one goal: run Debian GNU/Hurd
**x86_64** with all processors doing real work, and fix whatever breaks
when they do, in the kernel and in the Hurd userland above it.

Upstream GNU Mach builds SMP kernels, but puts the application processors
into a processor set of their own, so ordinary tasks only ever run on the
boot CPU.  Letting them use every CPU (the `smp_pset=default` option added
here) exposes a series of races in gnumach, the Hurd servers, glibc and the
rump disk driver.  This repository holds the fixes, the tests that find
them, and the tooling used to debug a hung Hurd from the outside.

The upstream GNU Mach README is unchanged in [`README`](README).

## Status

With every fix below, Debian GNU/Hurd amd64 (image 20250807) boots on
2 vCPUs with `smp_pset=default` and passes a parallel
build/sort/tar/find/fork stress test **6 times out of 6**.  Before the
glibc fix, 3 of 5 runs hung on a corrupted root file system server heap.

All gnumach tests (18, the new ones included) pass on an SMP kernel
(`-smp 2`) and on a uniprocessor kernel; the i386 configurations build
with no new warnings.

The patch series are ready in [`smp-dev/outbox/`](smp-dev/outbox) but
**not yet sent upstream** (bug-hurd@gnu.org, libc-alpha@sourceware.org).

## What was found

| Area | Problem | Effect | Fix |
|---|---|---|---|
| glibc (Hurd) | `_hurd_tls_new` never sets `tcbhead_t.multiple_threads`, so `catomic_*` (malloc fastbins) run **without `lock` prefix** in every thread but the main one | heap corruption in any multithreaded program on SMP, e.g. the root ext2fs | [glibc/0002](smp-dev/outbox/glibc) |
| glibc (Hurd) | `_Fork` resumes the signal thread before fetching its state; the two threads suspend each other | sporadic `/bin/sh` hang at boot | [glibc/0001](smp-dev/outbox/glibc) |
| libports | two threads can both see a port become unreferenced and both free it; payload lookups can take a reference on a port being freed | double free / use after free in every Hurd server | [hurd/0001-0002](smp-dev/outbox/hurd) |
| libfshelp | a record lock is freed while `F_SETLKW` waiters still sleep on it | use after free in ext2fs on concurrent `flock` | [hurd/0003](smp-dev/outbox/hurd) |
| libpager | control port used without the interlock while the object terminates | requests sent to a reused port name | [hurd/0004](smp-dev/outbox/hurd) |
| libddekit (netdde) | timer thread waits forever when the next timer is due in under 1 ms | network never comes up (DHCP hangs) | [netdde](smp-dev/outbox/netdde) |
| rumpkernel | `bus_dma` checks boundaries and coalesces segments on *virtual* addresses | DMA to wrong physical pages; `pciide_dma: buf align` panic | [rumpkernel](smp-dev/outbox/rumpkernel) |
| gnumach | per-cpu reads cached across `thread_sleep`; bad fs/gs bases accepted; `panic_lock` re-initialized; com tty used without its lock; threads of an empty pset never released | wrong CPU state, kernel faults, garbled console, stuck threads | [gnumach/0001-0005](smp-dev/outbox/gnumach) |
| gnumach | simple-lock debugging useless on SMP; other CPUs keep running on panic | — | [gnumach/0006-0009](smp-dev/outbox/gnumach) |

Ruled out on the way, each with a test that would catch it: lost TLB
shootdowns (`test-tlb`, verified to fail on a kernel without the shootdown
IPI), concurrent zero-fill faults (`test-fault`), brk-style remapping while
other CPUs hold the pages in their TLB (`test-brk`), glibc-style low-level
locks over gsync (`test-lll`), a malloc-like heap under one such lock
(`test-heap`), and preemption of busy threads (`test-preempt`).

The full investigation, hang by hang, is in
[`smp-dev/notes/hang-fork-suspend.md`](smp-dev/notes/hang-fork-suspend.md);
the initial inventory of the SMP code in
[`smp-dev/INVENTORY.md`](smp-dev/INVENTORY.md).

## Branches

| Branch | Content |
|---|---|
| `upstream` | mirror of git.savannah.gnu.org/git/hurd/gnumach.git |
| `main` | `upstream` + this README + [`smp-dev/`](smp-dev) |
| `submit-smp` | the 17-patch gnumach series as it goes to bug-hurd, linear on upstream master |
| `percpu-volatile`, `fsgs-base-check`, `panic-lock`, `com-tty-lock`, `pset-empty-release`, `smp-lock-debug`, `smp-stop-ipi`, `smp-default-pset` | one topic each, forked from `upstream` |
| `smp-tests` | `smp-default-pset` + the new SMP tests (`test-tlb` … `test-preempt`) |
| `smp-integration` | all topics merged: the kernel used for the Hurd boots and stress runs |

## Trying it

Everything runs in a Debian sid amd64 container (on Apple Silicon through
Rosetta); see [`smp-dev/README.md`](smp-dev/README.md) for the details.

```sh
smp-dev/build.sh                          # SMP kernel + make check under QEMU -smp 2
```

To boot Debian GNU/Hurd amd64 on several CPUs with this kernel, build the
`smp-integration` branch and boot it with `smp_pset=default` on the kernel
command line, as `smp-dev/hurd/mkiso.sh` does.  For a stable system you also
need the userland fixes above; until they are in Debian, the notes explain
how they were applied to a test image (binary patch of
`libpthread.so.0.3`, static servers relinked with
[`smp-dev/stress/tlsfix.c`](smp-dev/stress/tlsfix.c)).

Quick check whether a Hurd system has the glibc problem: build and run
[`smp-dev/stress/mtstress.c`](smp-dev/stress/mtstress.c) on 2 CPUs.  It
prints `worker multiple_threads=0` and usually aborts with a malloc
corruption message if the system is affected.

## Credits

Work by Magnus Reimann, done together with Claude (an AI model by
Anthropic); commits carry a `Co-Authored-By` trailer.  GNU Mach, the Hurd
and glibc are free software of the GNU project; see [`COPYING`](COPYING).

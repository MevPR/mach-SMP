# mach-SMP

## Purpose

This project makes **Debian GNU/Hurd on x86_64 use all processors of a
machine**, and fixes what breaks when it does.

GNU Mach, the Hurd's microkernel, can be built for SMP, but it puts all
processors except the boot CPU into a processor set of their own: ordinary
programs and Hurd servers only ever run on one CPU.  This fork lets them
run on every CPU, and fixes the bugs this exposes in the kernel and in
the Hurd userland on top of it (Hurd libraries, glibc, the disk driver).

Base: GNU Mach from git.savannah.gnu.org (branch `upstream`), Debian
GNU/Hurd amd64 image 20250807.  The original GNU Mach README is unchanged
in [`README`](README).

## Status

| | |
|---|---|
| **Kernel** | all CPUs usable with the boot option `smp_pset=default` |
| **Debian GNU/Hurd amd64, 2 CPUs** | boots, and passes a parallel build/sort/tar/find/fork stress test 6 times out of 6 (before the fixes: 3 of 5 runs hung) |
| **gnumach tests** | 18 of 18 pass, on an SMP kernel (2 CPUs) and on a uniprocessor kernel; i386 builds without new warnings |
| **Upstream** | all fixes prepared as patch series in [`smp-dev/outbox/`](smp-dev/outbox), **not yet sent** to bug-hurd@gnu.org and libc-alpha@sourceware.org |
| **Open** | one occurrence of "task ext2fs deallocating a bogus port" at boot, not yet explained; the rumpkernel fix is only tested in isolation; the glibc fixes were tested as binary patches, not as a glibc rebuild |

## What was fixed and improved

### GNU Mach (kernel)

- **New: `smp_pset=default`**: puts all processors into the default
  processor set at boot, so that every task can run on every CPU.
- **New: stop the other CPUs on panic and shutdown** (a dedicated
  interprocessor interrupt), so that a panic is not hidden by CPUs that
  keep running.
- **New: simple-lock debugging that works on SMP**: the lock-held checks
  were meaningless with several CPUs; holders are now tracked with
  `MACH_LDEBUG`.
- **Fixed:** per-CPU data read once and cached by the compiler across
  `thread_sleep`, so a thread that moved to another CPU used the old
  CPU's data.
- **Fixed:** fs/gs base addresses from user space were accepted unchecked;
  a kernel or non-canonical address made the kernel fault or defeated the
  `swapgs` check.
- **Fixed:** the panic lock was re-initialized on every panic.
- **Fixed:** the serial console driver used the tty without its lock from
  interrupt and timer handlers (garbled console output).
- **Fixed:** threads held for an empty processor set were never released.

### Hurd userland

- **glibc: heap corruption in every multithreaded program.** The thread
  control block of new threads never got `multiple_threads` set, so
  glibc's conditionally atomic operations (used by malloc) ran without the
  `lock` prefix in all threads but the main one.  Harmless on one CPU,
  heap corruption on several; this was the cause of the crashes and hangs
  of the root file system server.
- **glibc: fork deadlock.** `fork` resumed the signal thread before reading
  its state; the two threads could suspend each other (`/bin/sh` hung at
  boot).
- **libports:** two threads could both free a port that became
  unreferenced, and a message lookup could pick up a port being freed.
- **libfshelp:** a file record lock was freed while threads were still
  waiting on it (`flock`).
- **libpager:** the memory object control port was used while the object
  was being terminated.
- **libddekit (netdde):** the driver timer thread could wait forever when
  the next timer was due within 1 ms; the network never came up.
- **rumpkernel:** DMA segments were built from virtual instead of physical
  addresses, so disk transfers could go to the wrong memory pages (and
  rumpdisk panicked with `pciide_dma: buf align`).

### Tests and tools

- **New gnumach tests** for SMP: stress test, TLB shootdowns (verified to
  catch a kernel without shootdowns), concurrent page faults, brk-style
  remapping, glibc-style locks over gsync, a malloc-like heap, and
  preemption of busy threads.
- **Tools to debug a hung Hurd from outside**: gdb scripts that list what
  every thread waits for, who suspended whom, and write an ELF core of any
  task; a quarantining malloc for Hurd servers; a multithreaded malloc test
  that shows whether a system has the glibc problem.

## More

- [`smp-dev/notes/hang-fork-suspend.md`](smp-dev/notes/hang-fork-suspend.md):
  the investigation, problem by problem
- [`smp-dev/README.md`](smp-dev/README.md): branches, building, booting the
  Hurd under QEMU, debugging tools
- [`smp-dev/outbox/`](smp-dev/outbox): the patch series for upstream

Work by Magnus Reimann, done together with Claude (an AI model by
Anthropic).  GNU Mach is free software of the GNU project; see
[`COPYING`](COPYING).

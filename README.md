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
GNU/Hurd amd64 image 20250807, and Debian GNU/Hurd unstable of November
2026 (glibc 2.43) with [Chromium for the Hurd](https://github.com/MevPR/chromium-hurd).  The original GNU Mach README is unchanged
in [`README`](README).

| Repository | Component |
|---|---|
| **mach-SMP** (this one) | GNU Mach kernel; the investigation notes, tests and tools for the whole project |
| [hurd-SMP](https://github.com/MevPR/hurd-SMP) | Hurd libraries and servers (libports, libfshelp, libpager, pflocal, console-client) |
| [hurd-glibc](https://github.com/MevPR/hurd-glibc) | glibc's Hurd port |

The netdde (libddekit) and rumpkernel fixes are patches against Debian
packages and live in [`smp-dev/outbox/`](smp-dev/outbox) here.

## Status

| | |
|---|---|
| **Kernel** | all CPUs usable with the boot option `smp_pset=default` |
| **Debian GNU/Hurd amd64, 2 CPUs** | boots, and passes a parallel build/sort/tar/find/fork stress test 6 times out of 6 (before the fixes: 3 of 5 runs hung) |
| **gnumach tests** | 20 of 20 pass (test-multiboot skipped as before), on an SMP kernel (2 CPUs) and on a uniprocessor kernel; i386 builds without new warnings |
| **Chromium, 2 CPUs** | goal: open and render https://www.duckduckgo.com. Headless Chromium loads and renders it (in about 9 minutes under QEMU without KVM). In a window (Xvfb) the page does not load yet: Chromium's network service dies a few minutes after start and the first navigation never sends its request. Under investigation |
| **Upstream** | all fixes prepared as patch series in [`smp-dev/outbox/`](smp-dev/outbox) (22 gnumach patches), **not yet sent** to bug-hurd@gnu.org and libc-alpha@sourceware.org |
| **Open** | "task ext2fs looked up a bogus port" and a hang under Chromium load, with Debian's Hurd servers without the hurd-SMP fixes (being rebuilt with them); the rumpkernel fix is tested through a rebuilt object linked into rumpdisk, not a package rebuild; the glibc fixes were tested as binary patches, not as a glibc rebuild |

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
- **Fixed: the clock went backwards** by up to a tick on SMP: a reader
  could combine the time of a new tick with the high-precision counter
  value of the previous one.  Chromium's JavaScript engine aborted on it
  ("negative elapsed time").
- **Fixed: page flags lost on SMP.** The flags of a physical page that
  are protected by the page queues lock and those protected by the
  object lock were bit-fields of the same word; updating one kind could
  undo a concurrent update of the other, and the kernel panicked
  (`vm_page_seg_add_active_page` assertion) under Chromium.
- **Fixed:** when paging out to an external pager under memory pressure
  ("double paging"), the kernel reused the page after handing it to the
  pager, which on SMP may already be using or freeing it (found by
  inspection).

### Hurd userland

- **glibc ≤ 2.42: heap corruption in every multithreaded program**
  ([hurd-glibc](https://github.com/MevPR/hurd-glibc)).  The thread control
  block of new threads never got `multiple_threads` set, so glibc's
  conditionally atomic operations (used by malloc) ran without the `lock`
  prefix in all threads but the main one: heap corruption on SMP, e.g. in
  the root file system server.  glibc 2.43 removed these x86 `catomic_*`
  primitives (4eef002328, September 2025), which fixes it too; the patch is
  therefore only needed for older glibc and is no longer in the outbox.
- **glibc: fork deadlock.** `fork` resumed the signal thread before reading
  its state; the two threads could suspend each other (`/bin/sh` hung at
  boot).
- **libports** ([hurd-SMP](https://github.com/MevPR/hurd-SMP)): two threads could both free a port that became
  unreferenced, and a message lookup could pick up a port being freed.
- **libfshelp:** a file record lock was freed while threads were still
  waiting on it (`flock`).
- **pflocal:** when a socket and its address went away at the same time
  (processes dying at boot or shutdown), both were unbound twice and the
  Unix socket server aborted, taking every Unix socket of the system with
  it.
- **libpager:** the memory object control port was used while the object
  was being terminated.
- **libddekit (netdde):** the driver timer thread could wait forever when
  the next timer was due within 1 ms; the network never came up.
- **console-client:** when the Hurd console started more slowly than the
  20 s its parent waits (always the case under QEMU without KVM, where the
  vga driver parses a 5.7 MB font), it died of SIGPIPE when reporting
  success, and the system ran without a Hurd console.
- **rumpkernel:** DMA segments were built from virtual instead of physical
  addresses, so disk transfers could go to the wrong memory pages (and
  rumpdisk panicked with `pciide_dma: buf align`).

### Tests and tools

- **New gnumach tests** for SMP: stress test, TLB shootdowns (verified to
  catch a kernel without shootdowns), concurrent page faults, brk-style
  remapping, glibc-style locks over gsync, a malloc-like heap,
  preemption of busy threads, and a clock that must never go backwards.
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

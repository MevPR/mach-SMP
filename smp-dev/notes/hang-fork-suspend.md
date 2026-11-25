# Sporadic boot hang with smp_pset=default (2026-11-25)

Setup: Debian GNU/Hurd amd64 image 2025-08-07, `smp-integration` kernel,
`smp_pset=default`, QEMU `-smp 2` (TCG under Rosetta).

## Frequency

Boot series (`smp-dev/hurd/series.sh`), login reached:
- with com fix: 3/3 (plus 2 earlier hangs)
- without com fix: 2/3

So the hang is independent of the com patch, roughly one boot in three.
It happens early in runlevel S (after "Checking root file system ... done."
or after "Using makefile-style concurrent boot in runlevel S.").

## State of the hung system (`catch-gdb.sh`, then `ipcwait.py`, `otherwait.py`, `susp.py`)

- Both CPUs idle (`machine_idle`), run queues empty.
- 118 threads blocked in `mach_msg`; **none** of them waits on a port that
  already has a queued message, so this is not a lost IPC wakeup.
- The only threads not in `mach_msg` (apart from kernel threads) belong to
  two fork children of `/bin/sh(262)`:

```
(/bin/sh(262))   thread A state TH_SUSP          suspend_count 1 user_stop_count 1  ran 10 ms
(/bin/sh(262))   thread B state TH_SUSP|SWAPPED  suspend_count 1 user_stop_count 0  never ran
((/bin/sh(262))) 2 threads  TH_SUSP|SWAPPED  suspend_count 1 user_stop_count 1  never ran, rip 0
```

  Task suspend counts are 0.  Thread B carries a kernel-internal hold
  (thread_hold) without user stop and without any thread being in the
  middle of an operation on it (no thread waits on its wake_active event).

## Open questions

- Who issued the `thread_suspend` on A and the hold on B?  Needs user-space
  backtraces (glibc `fork` in sysdeps/mach/hurd/fork.c suspends the other
  threads of the forking task and creates the child's threads suspended).
- Candidate kernel races: thread_hold/thread_release vs. thread_dowait when
  the target last ran on the other processor; thread creation while the
  parent task is being held (task_hold iterates thread_list).

## How to reproduce

    docker run -d --name hangvm ... sh /hb/catch-gdb.sh /hb/smp-all.iso <kernel elf> 10
    docker exec hangvm gdb -batch -ex "file <elf>" -ex "target remote :1236" -ex "source /hb/susp.py"

## Update 2026-11-25: two distinct userland hangs, both exposed by SMP

A kernel built from branch `debug-hold-trace` (local only) records, per
thread, the last 16 changes of `suspend_count` with line, caller and the
current thread; `susphist.py` prints them.  Two different hang types were
caught:

### A. Mutual suspension of the two threads of a forked `/bin/sh` child

```
(/bin/sh(262)) M (main)   suspend_count 1 user_stop_count 1
   ... _Xthread_suspend by S                     <- S stopped M
(/bin/sh(262)) S (signal) suspend_count 1 user_stop_count 0
   ... thread_abort(S) by M, thread_resume(S) by M,
       thread_get_state(S) by M: hold +1, release never happens
```

M treats S as a thread to deliver a signal to (abort / resume /
get_state, the sequence glibc's signal thread applies to its target),
while S, the msgport thread, suspends M for the same purpose.  M blocks in
`thread_get_state` → `thread_dowait(S)` holding S; S is waiting for M.
Neither the kernel's suspend accounting nor IPC is wrong here: the kernel
does what it is told.  The race is in glibc's Hurd signal code in a fresh
fork child (two threads acting as signal thread at once); with real
parallelism the two paths overlap, on UP they practically never do.

### B. Root ext2fs stops itself

29 of 30 ext2fs threads carry `task_suspend`+`thread_suspend`+`task_resume`
(= `proc_dostop`); the remaining thread is the msgport thread.  That is
glibc `hurd/hurdsig.c:suspend()`, i.e. ext2fs received a stop signal
(SIGSTOP/SIGTSTP/SIGTTIN/SIGTTOU).  `_hurd_stopped` read from ext2fs memory
(`ext2fs-sig.py`, using the 20261028 ext2fs.static whose symbols are in
hurd-dbgsym) confirms the flag in such hangs.  Who sends the signal is
still open.

### Not a bug

`lostt.py` reported threads "waiting with RCV_TIMEOUT but no timer"; that
was a false positive: the mach_msg fast path (exactly SEND|RCV, no
timeout) does not update `ith_option`, so the saved option is stale.

### Real kernel bug found on the way

`processor_doaction` never released threads held for an empty processor
set (branch `pset-empty-release`, with `tests/test-pset`).

## Update 2026-11-25 (2): hang C root-caused, hang D seen

### C. DHCP never completes: libddekit timer thread sleeps forever

Chain (symbolized user backtraces from ELF cores of the hung tasks,
`mkcore.py` + `corefull.sh`, sysroot and dbgsym from
snapshot.debian.org/archive/debian-ports/20250807T000000Z):

    dhclient (daemon child)  main -> send_discover -> sendto -> socket_send  (to pfinet)
    pfinet RPC thread        S_socket_send -> pthread_mutex_lock(global_lock)
    pfinet timer thread      addrconf_dad_timer -> ndisc_send_ns -> dev_queue_xmit
                             -> ethernet_xmit -> device_write  (to netdde, holds global_lock)
    netdde eth0 (e1000)      state START|PRESENT|NOCARRIER, qdisc noop, 0 packets ever
    netdde ddekit timer      timer_list = { e1000_watchdog, expires 225 } but the
                             timer thread waits in ddekit_sem_down_timed(timo=0)

`ddekit_timer_thread` computes `to = (expires - jiffies) * 1000000 / HZ / 1000`,
which is 0 for a timer due within 1ms (or already due), and
`ddekit_sem_down_timed(sem, 0)` means *wait forever*.  The e1000 watchdog
never runs, the link never comes up.  Fix: `patches-hurd/0001-libddekit-...`
(libddekit is in the Debian hurd source and linked into libmachdevdde.so).

Hardware/kernel side verified clean in the hung VM: IOAPIC pin 11 unmasked,
no IRR / remote IRR, kernel user-intr entry for IRQ 11 n_unacked 0.

Comparison series, 6 boots each, 2 vCPUs, `smp_pset=default`:
original 1 DHCP hang; with the fix binary-patched into libmachdevdde.so
(`je` -> `nop` at 0x3ee5, test only) 0 DHCP hangs, but see D and one hang
of type A/B.

### D. rumpdisk panics on a misaligned DMA buffer (once)

    pciide_dma: seg 0 addr 0x7976f200 len 0x1800 not properly aligned
    panic: pciide_dma: buf align

Seen twice with the very same segment.  The address is suitably aligned;
the problem is that 0xf200 + 0x1800 crosses a 64KiB boundary, which IDE
bus-master DMA cannot do (NetBSD checks `(phys & ~0xffff) !=
((phys + len - 1) & ~0xffff)`).  The rump kernel's bus_dmamap_load on the
Hurd must split segments at the map's boundary (0x10000) and apparently
does not.  This is a rumpkernel/rumpdisk bug, not SMP specific, but the
changed timing/allocation pattern makes it show up.

### E. Root ext2fs crashes: the same heap chunk handed out twice

Thread in `exception_raise_continue` in ext2fs (core + `corefull.sh`;
note: glibc frames inside the static binary are mis-symbolized, Hurd
library frames are right):

    diskfs_S_dir_lookup("dev/urandom") -> diskfs_create_protid
      -> diskfs_finish_protid -> iohelp_dup_iouser (iouser-dup.c:43)
      -> idvec_set(dst = new->uids = NULL, ...)   page fault, cr2 = 0xc

`iohelp_dup_iouser` checked `new->uids != NULL` just before.  The 32-byte
chunk `new` (0x4000cb80, non-main arena) contains `{0x4000cb80, 0}`, i.e.
`new->uids` points to `new` itself: `make_idvec` got the very chunk `new`
from malloc again and initialised an idvec over it.  A chunk returned
twice means it was freed twice earlier.

Prime suspect: the libports race where two threads both see the
(hard 0, weak 1) transition and both run `_ports_complete_deallocate`,
which ends in `free (pi)` (libports/port-deref.c, complete-deallocate.c,
lookup-port.c); with real parallelism the window between dropping the
last hard reference and taking `_ports_htable_lock` for writing is open.
Not yet confirmed by a fix + test (needs rebuilding the Hurd libraries).

### Test of the libports fix (2026-11-25)

The fix was adapted to the deployed hurd 0.9.git20250801 (where the hash
table holds no weak reference and the final transition is (0, 0):
`patches-hurd/libports-final-deref-for-0.9.git20250801.diff`), and
ext2fs.static was rebuilt natively in a Hurd VM (container `devvm`,
`apt-get build-dep hurd`, `configure --enable-static-progs=ext2fs
MIG=x86_64-gnu-mig`).  Booted from the CD with the integration kernel,
`smp_pset=default`, 2 vCPUs, on the image that also has the libddekit fix:

    12 / 12 boots reached the login prompt

Earlier series with `smp_pset=default` (without the libports fix):
14 / 18.  Twelve successes in a row at that rate would happen by chance
with p ~ 0.05.  Hang types A and B did not show up either; they may have
been consequences of the same heap corruption, but that is not proven.

## Update 2026-11-25 (3): hang A root-caused (glibc), libports fix revised

### A. /bin/sh fork: the forking thread and the signal thread wait for each other

Symbolized cores of a stuck `(/bin/sh(32))` (a command substitution
subshell forking a pipeline):

    M: __thread_get_state(_hurd_msgport_thread) <- _Fork (_Fork.c:480) <- forkshell
    S: __thread_suspend(M) <- post_signal (hurdsig.c:1013, signo 20 = SIGCHLD)
       <- _S_msg_sig_post

`_Fork` resumes the threads it stopped with proc_dostop (resume_threads,
"all state has now been copied") *before* it fetches the signal thread's
state for the child (thread_get_state, and on x86_64 again in
_hurd_tls_fork).  The signal thread, running again, delivers SIGCHLD:
post_signal suspends the target first and only then checks the critical
section.  M is suspended while waiting in thread_get_state(S); S is then
stopped by thread_get_state's hold; deadlock.  Fix:
`patches-hurd/0001-hurd-fork-fetch-the-signal-thread-state-before-resum.patch`
(glibc; move resume_threads after the signal thread setup).  Untested so far
(needs a glibc rebuild).

### E (revised). libports: a first "defer the no-senders deref" attempt hung

Deferring the whole release on no-senders (as ports_destroy_right does)
made pflocal never close the write end of a pipe: the deferred reference
is only dropped once every thread of the bucket passes a quiescent state,
and an idle thread blocked in receive never does (seen as
`old_objects = {sock_user}` with `old_threads = 1`, the rc shell waiting
for EOF forever).  Replaced by: release immediately, defer only `free(pi)`,
and make ports_lookup_payload refuse to resurrect a dying port
(`refcounts_ref_unless_dying`).  Patch 0002-libports-....

## Update 2026-11-25 (4): heap corruption under stress found (libfshelp), libpager race

With every fix so far installed, a stress run (parallel gcc, sort, tar,
find, fork loops, 2 vCPUs) still corrupted the root ext2fs heap.  ext2fs
was relinked with a quarantine allocator (`hurdboot/dbgmalloc.c`, linked
with `-Wl,--wrap=malloc,...`: freed blocks are poisoned with 0xa5 and kept
in a ring).  It then faulted in `fshelp_rlock_tweak` (unlock_region)
walking `l = 0xa5a5...` while serving `file_lock` on
`/var/lib/wtmpdb/wtmp.db`.

### F. libfshelp record locks: two use-after-free bugs

1. `unlock_region` and its callers read `l->po.next` after `rele_lock (l)`
   freed `l`.  Already fixed upstream on 2026-03-22 (8d021470, "libfshelp:
   Free Memory Read errors after calls to rele_lock()"), but missing from
   Debian's 0.9.git20250801, which is what crashed here.
2. New: `rele_lock` and `fshelp_rlock_drop_peropen` broadcast a lock's
   `wait` condition and then destroy and free the lock, while the
   `F_SETLKW64` waiters are still asleep on it.  Their wake-up then
   touches freed memory.  Fix: `waiting` counts the sleepers; a lock that
   is released while it has waiters is only marked `dead`, and the last
   waiter to wake up frees it.  Patch
   `patches-hurd/0001-libfshelp-do-not-free-a-record-lock-...`.

Both bugs are SMP-sensitive only in the sense that the waiters and the
unlocker now really run at the same time.

### G. libpager uses the control port while the object terminates

gnumach logged `task ext2fs looked up a bogus port 652 for 2095` (2095 is
`memory_object_change_attributes`).  `pager_change_attributes` and
`_pager_lock_object` drop the interlock and then send on
`p->memobjcntl`; `memory_object_terminate` can run in between, destroy
the control port, and the name may be reused.  Fix: block termination
around the request (`_pager_block_termination`), as data_request and
data_return already do.  Patch `patches-hurd/0001-libpager-...`.

### Stress results with F and G fixed

Runs 8 and 10 clean.  Runs 9 and 11 hung (seq/sort/tar phase, all
threads waiting for replies).  Run 11, kept alive and inspected: one
root ext2fs thread trapped in `dbgmalloc report ("double free of")`:

    _ports_complete_deallocate (protid) -> diskfs_protid_rele
      -> iohelp_free_iouser -> idvec_free (iouser->gids) -> free (gids->ids)

The 4-byte block in `gids->ids` had been freed ~7800 frees earlier as
`uids->ids` of *another* iouser, still in quarantine.  So glibc's malloc
returned one chunk to two `idvec_set` calls: hang E again, with the
libports fix and both F fixes in place.  All 34 ext2fs threads have
distinct fs bases in their PCBs (so not shared TLS / tcache).

dbgmalloc v1 itself could hide the cause: its "already freed?" check was
not atomic, so two threads freeing one block concurrently both put it
into the ring, which later really frees it twice -> exactly a chunk
handed out twice.  dbgmalloc v2 claims a block with a LIVE -> FREED
compare-and-swap and keeps the last report in `dbgmalloc_last` for
cores.  Next run should name the concurrent double free, if that is it.

## Update 2026-11-25 (5): ROOT CAUSE of the heap corruption (hang E): glibc catomic without lock prefix

A plain multithreaded test (`hurdboot/mtstress.c`: malloc/free with
pattern checks) aborts within seconds on 2 vCPUs ("malloc_consolidate():
unaligned fastbin chunk detected", "corrupted size vs. prev_size in
fastbins"), never on 1 vCPU.  Also with MALLOC_ARENA_MAX=1 and tcache off.
The same lock/heap pattern written by hand (gnumach tests test-lll,
test-heap; `hurdboot/heapsim.c` on the Hurd) never fails, so the kernel
and gsync are fine.

glibc's malloc pushes freed chunks onto fastbins with
`catomic_compare_and_exchange_val_rel`, which on x86 is

    cmpl $0, %fs:multiple_threads ; je 0f ; lock ; 0: cmpxchg

On the Hurd only `_hurd_tls_init` (main thread) sets
`tcbhead_t.multiple_threads`; `_hurd_tls_new`, used for every new
pthread, does not, and `_dl_allocate_tls` zeroes the TCB.  So in all
threads but the main one the cmpxchg runs without `lock`: atomic on one
CPU, racy on several.  mtstress prints `multiple_threads=0` in its
workers; setting `%fs:0x18 = 1` at the start of each worker makes every
run pass (12/12 vs 0/6).

Fix: set `tcb->multiple_threads = 1` in `_hurd_tls_new`
(sysdeps/mach/hurd/{x86_64,i386}/tls.h).  Patch
`patches-hurd/0001-hurd-set-multiple_threads-in-new-threads-TCB.patch`.

### Test of the fix

Binary patch of Debian's libpthread.so.0.3 (2.41, 20250807; htl still
lives there): at 0x66c3, `tcb->tcb = tcb` jumps to a 22-byte cave in the
alignment gap after .fini (0xc248, executable segment) that also does
`movl $1, 0x18(%rax)`.  Result on 2 vCPUs: mtstress workers report
`multiple_threads=1` and every run passes (MALLOC_ARENA_MAX=1 no-pool,
default, 8 threads), where the same runs failed every time before.

Static binaries (ext2fs.static) carry their own copy of htl: relinked
with `smp-dev/stress/tlsfix.c` and `-Wl,--wrap=_dl_allocate_tls`.

Tools: `smp-dev/stress/mtstress.c` (malloc torture, modes),
`smp-dev/stress/heapsim.c` (lll lock + brk-style heap without glibc
malloc), gnumach tests test-tlb, test-fault, test-brk, test-lll,
test-heap (branch tlb-shootdown-test).

### Full stress test with every fix (2026-11-25)

Fixed libpthread.so (binary patch above), ext2fs.static relinked with
tlsfix.c + dbgmalloc v2, plus the libfshelp rlock, libpager, libports,
libddekit and glibc fork fixes; integration kernel, `smp_pset=default`,
2 vCPUs; `cmds-stress.txt` (4 parallel gcc, sort, tar/find, fork loops):

    6 / 6 runs complete (runs 18-23), no dbgmalloc report, no crash,
    no "looked up a bogus port"

Before the multiple_threads fix: runs 8-12 gave 2 complete, 3 hung on
ext2fs heap corruption.

Left: once (run 18, during boot, after `/usr/bin/console` timed out),
gnumach printed "task ext2fs deallocating a bogus port 946" - not
analysed yet.

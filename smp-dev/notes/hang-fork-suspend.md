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

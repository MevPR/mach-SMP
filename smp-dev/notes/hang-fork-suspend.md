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

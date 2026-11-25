/*
 *  Copyright (C) 2026 Free Software Foundation
 *
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the program ; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Shrink and grow memory the way glibc's brk does on the Hurd
 * (vm_deallocate, vm_map without access, vm_protect read/write), while
 * other threads have the old pages in their TLB.  In each round the
 * workers first read and write the pages, then wait (spinning or
 * sleeping, so that their processor may go idle) while the main thread
 * replaces the memory, then check that they see the new contents and
 * that their writes reach the new pages.  A processor that missed the
 * TLB shootdown keeps using the freed physical pages.
 */

#include <mach/thread_info.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>

#define NWORKERS	3
#define PAGES		8
#define ROUNDS		1000
#define MAXCPUS		64

static volatile unsigned long cpus_seen;
static volatile int phase, acks, done;
static volatile long stale, stale_round, stale_got, lost;
static vm_address_t region;
static int ncpus;

/* See test-smp.c.  */
static void assign_all_processors_to_default_pset(void)
{
  processor_array_t procs;
  mach_msg_type_number_t nprocs;
  processor_set_name_t default_name, cur;
  processor_set_t default_pset;
  int err;

  err = host_processors(host_priv(), &procs, &nprocs);
  ASSERT_RET(err, "host_processors");
  ncpus = nprocs;
  printf("%d processors\n", nprocs);

  err = processor_set_default(mach_host_self(), &default_name);
  ASSERT_RET(err, "processor_set_default");
  err = host_processor_set_priv(host_priv(), default_name, &default_pset);
  ASSERT_RET(err, "host_processor_set_priv");

  for (int i = 0; i < nprocs; i++)
    {
      err = processor_get_assignment(procs[i], &cur);
      ASSERT_RET(err, "processor_get_assignment");
      if (cur == default_name)
        continue;
      err = processor_assign(procs[i], default_pset, TRUE);
      ASSERT_RET(err, "processor_assign");
    }
}

static void note_cpu(void)
{
  struct thread_sched_info info;
  mach_msg_type_number_t count = THREAD_SCHED_INFO_COUNT;
  int err;

  err = thread_info(mach_thread_self(), THREAD_SCHED_INFO,
                    (thread_info_t) &info, &count);
  ASSERT_RET(err, "thread_info");
  if (info.last_processor >= 0 && info.last_processor < MAXCPUS)
    __atomic_or_fetch(&cpus_seen, 1UL << info.last_processor,
                      __ATOMIC_RELAXED);
}

/* Spin for a while, then sleep: sleeping lets the processor go idle,
   which is a separate path for TLB shootdowns.  */
#define WAIT_UNTIL(cond, sleepy)                        \
  for (long _n = 0; ! (cond); _n++)                     \
    if ((sleepy) || _n > 20000)                         \
      msleep(1)

static inline volatile long *word(int page, long id)
{
  return (volatile long *) (region + page * vm_page_size) + id;
}

static void worker(void *arg)
{
  long id = (long) arg;

  for (int r = 1; r <= ROUNDS; r++)
    {
      int sleepy = (r + id) % 4 == 0;

      /* Phase 2r - 1: the memory holds round R; use it.  */
      WAIT_UNTIL(__atomic_load_n(&phase, __ATOMIC_ACQUIRE) >= 2 * r - 1, 0);
      for (int pg = 0; pg < PAGES; pg++)
        {
          long v = *word(pg, 0);
          if (v != r && __atomic_add_fetch(&stale, 1, __ATOMIC_RELAXED) == 1)
            {
              stale_round = r;
              stale_got = v;
            }
          /* Our own word: the main thread checks it was not lost.  */
          *word(pg, 1 + id) = r;
        }
      __atomic_add_fetch(&acks, 1, __ATOMIC_RELEASE);

      /* Phase 2r: the main thread replaces the memory; keep away.  */
      WAIT_UNTIL(__atomic_load_n(&phase, __ATOMIC_ACQUIRE) >= 2 * r + 1
                 || r == ROUNDS, sleepy);
      if (r % 100 == 0)
        note_cpu();
    }
  __atomic_add_fetch(&done, 1, __ATOMIC_RELEASE);
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  vm_size_t size = PAGES * vm_page_size;
  int nseen = 0;
  int err;

  assign_all_processors_to_default_pset();

  err = vm_allocate(mach_task_self(), &region, size, TRUE);
  ASSERT_RET(err, "vm_allocate");

  for (long i = 0; i < NWORKERS; i++)
    test_thread_start(mach_task_self(), worker, (void *) i);

  for (int r = 1; r <= ROUNDS; r++)
    {
      if (r > 1)
        {
          /* Shrink, as _hurd_set_brk does...  */
          err = vm_deallocate(mach_task_self(), region, size);
          ASSERT_RET(err, "vm_deallocate");
          vm_address_t addr = region;
          err = vm_map(mach_task_self(), &addr, size, 0, FALSE,
                       MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
                       VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
                       VM_INHERIT_COPY);
          ASSERT_RET(err, "vm_map");
          ASSERT(addr == region, "vm_map moved the region");
          /* ...and grow again.  */
          err = vm_protect(mach_task_self(), region, size, FALSE,
                           VM_PROT_READ | VM_PROT_WRITE);
          ASSERT_RET(err, "vm_protect");
        }
      for (int pg = 0; pg < PAGES; pg++)
        *word(pg, 0) = r;

      __atomic_store_n(&acks, 0, __ATOMIC_RELAXED);
      __atomic_store_n(&phase, 2 * r - 1, __ATOMIC_RELEASE);
      WAIT_UNTIL(__atomic_load_n(&acks, __ATOMIC_ACQUIRE) >= NWORKERS, 0);

      /* The workers' writes must be in the memory we see.  */
      for (int pg = 0; pg < PAGES; pg++)
        for (long id = 0; id < NWORKERS; id++)
          if (*word(pg, 1 + id) != r)
            lost++;
      __atomic_store_n(&phase, 2 * r, __ATOMIC_RELEASE);
    }

  WAIT_UNTIL(__atomic_load_n(&done, __ATOMIC_ACQUIRE) >= NWORKERS, 1);

  for (int i = 0; i < MAXCPUS; i++)
    if (cpus_seen & (1UL << i))
      nseen++;
  printf("%d rounds, %ld stale reads, %ld lost writes, workers ran on %d of %d processors (mask %lx)\n",
         ROUNDS, stale, lost, nseen, ncpus, cpus_seen);
  if (stale)
    printf("first stale read: round %ld got %ld\n", stale_round, stale_got);
  ASSERT(stale == 0 && lost == 0, "a processor used freed pages");

  return 0;
}

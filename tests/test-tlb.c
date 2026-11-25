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
 * Check that TLB shootdowns reach every processor: reader threads keep
 * a page in their TLB, while a writer thread keeps replacing the memory
 * behind it with vm_copy, which unmaps the old physical page and frees
 * it.  The writer stores a generation number in each new page before
 * publishing it; a reader that sees the published generation and then
 * reads an older one from the page is using a stale TLB entry.
 */

#include <mach/thread_info.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>

#define NREADERS	3
#define ROUNDS		4000
#define MAXCPUS		64

static volatile long generation;
static volatile int stop;
static volatile int readers_done;
static volatile unsigned long cpus_seen;
static volatile long stale_seen, stale_got, stale_want;
static volatile long reads;

static vm_address_t page, source;
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

static void reader(void *arg)
{
  long n = 0;

  (void) arg;
  while (! __atomic_load_n(&stop, __ATOMIC_ACQUIRE))
    {
      long want = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
      long got = __atomic_load_n((long *) page, __ATOMIC_ACQUIRE);

      if (got < want)
        {
          /* Keep the first one, the others are likely the same entry.  */
          if (__atomic_add_fetch(&stale_seen, 1, __ATOMIC_RELAXED) == 1)
            {
              stale_got = got;
              stale_want = want;
            }
        }
      if (++n % 100000 == 0)
        note_cpu();
    }
  note_cpu();
  __atomic_add_fetch(&reads, n, __ATOMIC_RELAXED);
  __atomic_add_fetch(&readers_done, 1, __ATOMIC_RELEASE);
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  int nseen = 0;
  int err;

  assign_all_processors_to_default_pset();

  err = vm_allocate(mach_task_self(), &page, vm_page_size, TRUE);
  ASSERT_RET(err, "vm_allocate");
  err = vm_allocate(mach_task_self(), &source, vm_page_size, TRUE);
  ASSERT_RET(err, "vm_allocate");
  *(volatile long *) page = 0;

  for (long i = 0; i < NREADERS; i++)
    test_thread_start(mach_task_self(), reader, (void *) i);

  for (long g = 1; g <= ROUNDS; g++)
    {
      /* Give the page a new physical page holding G...  */
      *(volatile long *) source = g;
      err = vm_copy(mach_task_self(), source, vm_page_size, page);
      ASSERT_RET(err, "vm_copy");
      /* ...and only then tell the readers to expect it.  */
      __atomic_store_n(&generation, g, __ATOMIC_RELEASE);
      if (g % 500 == 0)
        note_cpu();
    }

  __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&readers_done, __ATOMIC_ACQUIRE) < NREADERS)
    msleep(10);

  for (int i = 0; i < MAXCPUS; i++)
    if (cpus_seen & (1UL << i))
      nseen++;
  printf("%d rounds, %ld reads, threads ran on %d of %d processors (mask %lx)\n",
         ROUNDS, reads, nseen, ncpus, cpus_seen);
  if (stale_seen)
    printf("%ld stale reads, first: got generation %ld, expected %ld\n",
           stale_seen, stale_got, stale_want);
  ASSERT(stale_seen == 0, "reader saw a stale page");

  return 0;
}

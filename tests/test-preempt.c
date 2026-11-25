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
 * Run more busy-looping threads than there are processors, and check
 * that every one of them keeps making progress, and that the main
 * thread, which sleeps in between, gets to run again: threads that
 * never block must still be preempted at the end of their quantum, on
 * every processor.
 */

#include <mach/thread_info.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>

#define MAXWORKERS	32
#define ROUNDS		5
#define MAXCPUS		64

static volatile long progress[MAXWORKERS];
static volatile unsigned long cpus_seen[MAXWORKERS];
static volatile int stop;
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

static int last_cpu(void)
{
  struct thread_sched_info info;
  mach_msg_type_number_t count = THREAD_SCHED_INFO_COUNT;
  int err;

  err = thread_info(mach_thread_self(), THREAD_SCHED_INFO,
                    (thread_info_t) &info, &count);
  ASSERT_RET(err, "thread_info");
  return info.last_processor;
}

static void spinner(void *arg)
{
  long id = (long) arg;

  while (! __atomic_load_n(&stop, __ATOMIC_ACQUIRE))
    {
      /* Pure user-mode work, no system call except now and then.  */
      for (int k = 0; k < 100000; k++)
        __asm__ volatile ("" ::: "memory");
      __atomic_add_fetch(&progress[id], 1, __ATOMIC_RELAXED);
      if ((progress[id] & 63) == 0)
        {
          int cpu = last_cpu();
          if (cpu >= 0 && cpu < MAXCPUS)
            cpus_seen[id] |= 1UL << cpu;
        }
    }
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  long last[MAXWORKERS] = { 0 };
  int nworkers, starved = 0;

  assign_all_processors_to_default_pset();
  nworkers = 2 * ncpus + 1;
  if (nworkers > MAXWORKERS)
    nworkers = MAXWORKERS;

  for (long i = 0; i < nworkers; i++)
    test_thread_start(mach_task_self(), spinner, (void *) i);

  for (int r = 1; r <= ROUNDS; r++)
    {
      /* If the main thread is starved, this never returns and the test
         times out.  */
      msleep(1000);
      int stuck = 0;
      for (int i = 0; i < nworkers; i++)
        {
          if (progress[i] == last[i])
            stuck++;
          last[i] = progress[i];
        }
      printf("round %d: %d of %d spinners made no progress\n",
             r, stuck, nworkers);
      if (r > 1)
        starved += stuck;
    }
  __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);

  for (int i = 0; i < nworkers; i++)
    printf("spinner %d: %ld, cpus %lx\n", i, progress[i], cpus_seen[i]);
  ASSERT(starved == 0, "some spinners were starved");

  return 0;
}

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
 * A thread assigned to a processor set without processors is held, and
 * must run again once a processor is assigned to that set.
 */

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>

static volatile int counter;

static void worker(void *arg)
{
  for (;;)
    {
      __atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED);
      msleep(10);
    }
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  processor_array_t procs;
  mach_msg_type_number_t nprocs;
  processor_set_t new_pset, default_pset;
  processor_set_name_t new_name, default_name, cur;
  thread_t th;
  int err, c1, c2, ap = -1;

  err = host_processors(host_priv(), &procs, &nprocs);
  ASSERT_RET(err, "host_processors");
  err = processor_set_default(mach_host_self(), &default_name);
  ASSERT_RET(err, "processor_set_default");
  err = host_processor_set_priv(host_priv(), default_name, &default_pset);
  ASSERT_RET(err, "host_processor_set_priv");

  /* Any processor but the master can be reassigned.  */
  for (int i = 1; i < nprocs; i++)
    ap = i;
  err = processor_set_create(host_priv(), &new_pset, &new_name);
  if (ap < 0 || err)
    {
      printf("needs a second processor and processor sets (%d processors,"
             " processor_set_create: %s), skipping\n", nprocs, e2s(err));
      return 0;
    }

  th = test_thread_start(mach_task_self(), worker, NULL);
  msleep(100);
  ASSERT(counter > 0, "worker did not start");

  /* The new set has no processors: the worker must stop.  */
  err = thread_assign(th, new_pset);
  ASSERT_RET(err, "thread_assign");
  msleep(100);
  c1 = counter;
  msleep(300);
  c2 = counter;
  printf("in empty set: %d -> %d\n", c1, c2);
  ASSERT(c1 == c2, "thread ran in a processor set without processors");

  /* Give the set a processor: the worker must run again.  */
  err = processor_assign(procs[ap], new_pset, TRUE);
  ASSERT_RET(err, "processor_assign");
  err = processor_get_assignment(procs[ap], &cur);
  ASSERT_RET(err, "processor_get_assignment");
  ASSERT(cur == new_name, "processor not assigned");
  msleep(300);
  printf("with a processor: %d -> %d\n", c2, counter);
  ASSERT(counter > c2, "thread still held after assigning a processor");

  err = thread_assign(th, default_pset);
  ASSERT_RET(err, "thread_assign back");
  err = processor_assign(procs[ap], default_pset, TRUE);
  ASSERT_RET(err, "processor_assign back");
  return 0;
}

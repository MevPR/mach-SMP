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
 * Run threads of one task on all processors at once, doing VM and IPC
 * operations on the shared address space and port name space.
 */

#include <mach/message.h>
#include <mach/thread_info.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>
#include <mach_port.user.h>

#define NWORKERS	8
#define ITERATIONS	500
#define MAXCPUS		64

static volatile int workers_done;
static volatile unsigned long cpus_seen;

static int ncpus;

/*
 * The APs start in a processor set of their own: move every processor to
 * the default one, which our threads are in.
 */
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
      err = processor_get_assignment(procs[i], &cur);
      ASSERT_RET(err, "processor_get_assignment");
      ASSERT(cur == default_name, "processor not moved to default pset");
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

static void vm_round(void)
{
  vm_address_t addr = 0;
  int err;

  err = vm_allocate(mach_task_self(), &addr, 4 * vm_page_size, TRUE);
  ASSERT_RET(err, "vm_allocate");
  for (int i = 0; i < 4; i++)
    ((volatile char *) addr)[i * vm_page_size] = i;
  err = vm_protect(mach_task_self(), addr, vm_page_size, FALSE,
                   VM_PROT_READ);
  ASSERT_RET(err, "vm_protect");
  err = vm_deallocate(mach_task_self(), addr, 4 * vm_page_size);
  ASSERT_RET(err, "vm_deallocate");
}

static void ipc_round(void)
{
  struct {
    mach_msg_header_t head;
    char body[64];
  } msg;
  mach_port_t port;
  int err;

  err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
  ASSERT_RET(err, "mach_port_allocate");

  msg.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
  msg.head.msgh_size = sizeof msg.head;
  msg.head.msgh_remote_port = port;
  msg.head.msgh_local_port = MACH_PORT_NULL;
  msg.head.msgh_id = 42;
  err = mach_msg(&msg.head, MACH_SEND_MSG | MACH_RCV_MSG, sizeof msg.head,
                 sizeof msg, port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  ASSERT_RET(err, "mach_msg");
  ASSERT(msg.head.msgh_id == 42, "wrong message received");

  err = mach_port_mod_refs(mach_task_self(), port,
                           MACH_PORT_RIGHT_RECEIVE, -1);
  ASSERT_RET(err, "mach_port_mod_refs");
}

static void worker(void *arg)
{
  for (int i = 0; i < ITERATIONS; i++)
    {
      vm_round();
      ipc_round();
      if (i % 16 == 0)
        note_cpu();
    }
  note_cpu();

  __atomic_add_fetch(&workers_done, 1, __ATOMIC_RELEASE);
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  int nseen = 0;

  assign_all_processors_to_default_pset();

  for (long i = 0; i < NWORKERS; i++)
    test_thread_start(mach_task_self(), worker, (void *) i);

  while (__atomic_load_n(&workers_done, __ATOMIC_ACQUIRE) < NWORKERS)
    msleep(100);

  for (int i = 0; i < MAXCPUS; i++)
    if (cpus_seen & (1UL << i))
      nseen++;
  printf("workers ran on %d of %d processors (mask %lx)\n",
         nseen, ncpus, cpus_seen);
  if (ncpus > 1)
    ASSERT(nseen > 1, "workers did not run on more than one processor");

  return 0;
}

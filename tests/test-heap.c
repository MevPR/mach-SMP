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
 * Mimic glibc's malloc with a single arena on the Hurd: threads on all
 * processors contend for one low-level lock (compare-and-swap, exchange,
 * gsync_wait/gsync_wake) protecting a "heap" that grows and shrinks like
 * the brk area (vm_protect to grow, vm_deallocate plus vm_map without
 * access to shrink).  Under the lock, threads check and rewrite records
 * spread over the heap pages.  Any record that does not hold what its
 * last writer put there means the lock failed or memory went astray.
 */

#include <mach/thread_info.h>
#include <mach/machine/vm_param.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>
#include <mach_host.user.h>
#include <gnumach.user.h>

#define NWORKERS	4
#define ITERATIONS	50000
#define HEAP_PAGES	64
#define MAXCPUS		64

/* One record per 512 bytes; a record is RECORD_WORDS longs.  */
#define RECORD_STRIDE	(512 / sizeof (long))
#define RECORD_WORDS	8

static int heap_lock;
static vm_address_t heap;
static int heap_pages;		/* pages accessible, protected by heap_lock */
static long seq;		/* protected by heap_lock */

static volatile unsigned long cpus_seen;
static volatile int done;
static volatile long bad, bad_page, bad_word, bad_got, bad_want;
static volatile long grows, shrinks, waits;
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

/* As __lll_lock and __lll_unlock in glibc's mach/lowlevellock.h.  */
static void lll_lock(int *ptr)
{
  int expect = 0;

  if (__atomic_load_n(ptr, __ATOMIC_RELAXED) != 0
      || ! __atomic_compare_exchange_n(ptr, &expect, 1, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    while (1)
      {
        if (__atomic_exchange_n(ptr, 2, __ATOMIC_ACQUIRE) == 0)
          break;
        __atomic_add_fetch(&waits, 1, __ATOMIC_RELAXED);
        gsync_wait(mach_task_self(), (vm_offset_t) ptr, 2, 0, 0, 0);
      }
}

static void lll_unlock(int *ptr)
{
  if (__atomic_exchange_n(ptr, 0, __ATOMIC_RELEASE) == 2)
    gsync_wake(mach_task_self(), (vm_offset_t) ptr, 0, 0);
}

static inline volatile long *record(int page, int r)
{
  return (volatile long *) (heap + page * vm_page_size) + r * RECORD_STRIDE;
}

/* Records of a page hold, in word 0, the sequence number of their last
   write (0 for a page that was never written since it was last made
   accessible), and in the other words values derived from it.  */
static void check_record(int page, int r)
{
  volatile long *p = record(page, r);
  long s = p[0];

  for (int w = 1; w < RECORD_WORDS; w++)
    {
      long want = s ? s * 31 + w : 0;
      if (p[w] != want && __atomic_add_fetch(&bad, 1, __ATOMIC_RELAXED) == 1)
        {
          bad_page = page;
          bad_word = r * RECORD_STRIDE + w;
          bad_got = p[w];
          bad_want = want;
        }
    }
}

static void write_record(int page, int r)
{
  volatile long *p = record(page, r);
  long s = ++seq;

  for (int w = 1; w < RECORD_WORDS; w++)
    p[w] = s * 31 + w;
  p[0] = s;
}

static void grow(int pages)
{
  int err;

  err = vm_protect(mach_task_self(), heap + heap_pages * vm_page_size,
                   pages * vm_page_size, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE);
  ASSERT_RET(err, "vm_protect");
  heap_pages += pages;
  grows++;
}

static void shrink(int pages)
{
  vm_address_t addr = heap + (heap_pages - pages) * vm_page_size;
  vm_size_t size = pages * vm_page_size;
  int err;

  /* As _hurd_set_brk does.  */
  err = vm_deallocate(mach_task_self(), addr, size);
  ASSERT_RET(err, "vm_deallocate");
  err = vm_map(mach_task_self(), &addr, size, 0, FALSE, MACH_PORT_NULL, 0,
               FALSE, VM_PROT_NONE,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
               VM_INHERIT_COPY);
  ASSERT_RET(err, "vm_map");
  heap_pages -= pages;
  shrinks++;
}

static void worker(void *arg)
{
  long id = (long) arg;
  unsigned r = id * 7919 + 1;

  for (int i = 0; i < ITERATIONS; i++)
    {
      r = r * 1103515245 + 12345;

      lll_lock(&heap_lock);
      if ((r >> 4) % 64 == 0 && heap_pages > 2)
        shrink(1 + (r >> 10) % (heap_pages / 2));
      else if ((r >> 4) % 16 == 1 && heap_pages < HEAP_PAGES)
        grow(1 + (r >> 10) % (HEAP_PAGES - heap_pages));
      for (int k = 0; k < 4; k++)
        {
          r = r * 1103515245 + 12345;
          int page = (r >> 8) % heap_pages;
          int rec = (r >> 16) % (vm_page_size / 512);
          check_record(page, rec);
          write_record(page, rec);
        }
      lll_unlock(&heap_lock);

      if (i % 5000 == 0)
        note_cpu();
    }
  __atomic_add_fetch(&done, 1, __ATOMIC_RELEASE);
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  int nseen = 0;
  int err;

  assign_all_processors_to_default_pset();

  /* Reserve the heap without access, as glibc reserves the brk area.  */
  err = vm_map(mach_task_self(), &heap, HEAP_PAGES * vm_page_size, 0, TRUE,
               MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
               VM_INHERIT_COPY);
  ASSERT_RET(err, "vm_map");
  grow(4);

  for (long i = 0; i < NWORKERS; i++)
    test_thread_start(mach_task_self(), worker, (void *) i);

  while (__atomic_load_n(&done, __ATOMIC_ACQUIRE) < NWORKERS)
    msleep(10);

  for (int i = 0; i < MAXCPUS; i++)
    if (cpus_seen & (1UL << i))
      nseen++;
  printf("%d iterations, %ld grows, %ld shrinks, %ld waits, %ld bad words, workers ran on %d of %d processors (mask %lx)\n",
         NWORKERS * ITERATIONS, grows, shrinks, waits, bad, nseen, ncpus,
         cpus_seen);
  if (bad)
    printf("first bad word: page %ld word %ld is %lx, want %lx\n",
           bad_page, bad_word, bad_got, bad_want);
  ASSERT(bad == 0, "heap records corrupted");

  return 0;
}

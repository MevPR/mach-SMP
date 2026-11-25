/* Hurd userland port of gnumach tests/test-heap.c.  */
#include <mach.h>
#include <mach/gnumach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define vm_page_size 4096
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
  if (err) abort ();
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
  if (err) abort ();
  err = vm_map(mach_task_self(), &addr, size, 0, FALSE, MACH_PORT_NULL, 0,
               FALSE, VM_PROT_NONE,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
               VM_INHERIT_COPY);
  if (err) abort ();
  heap_pages -= pages;
  shrinks++;
}

static void *worker(void *arg)
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

    }
  __atomic_add_fetch(&done, 1, __ATOMIC_RELEASE);
  return NULL;
}

int main(void)
{

  int err;

  /* Reserve the heap without access, as glibc reserves the brk area.  */
  err = vm_map(mach_task_self(), &heap, HEAP_PAGES * vm_page_size, 0, TRUE,
               MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
               VM_INHERIT_COPY);
  if (err) abort ();
  grow(4);

  pthread_t t[NWORKERS];
  for (long i = 0; i < NWORKERS; i++)
    pthread_create(&t[i], NULL, worker, (void *) i);
  for (int i = 0; i < NWORKERS; i++)
    pthread_join(t[i], NULL);
  printf("%d iterations, %ld grows, %ld shrinks, %ld waits, %ld bad words\n",
         NWORKERS * ITERATIONS, grows, shrinks, waits, bad);
  if (bad)
    printf("first bad word: page %ld word %ld is %lx, want %lx\n",
           bad_page, bad_word, bad_got, bad_want);
  puts(bad ? "HEAPSIM-BAD" : "HEAPSIM-OK");

  return 0;
}

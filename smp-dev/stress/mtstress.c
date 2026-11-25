/* Multithreaded malloc / mutex / memcpy torture test for SMP.

   Each thread keeps SLOTS blocks filled with a pattern derived from its
   address and a sequence number, checks the pattern before freeing,
   hands some blocks to a shared pool that other threads free, copies
   blocks with memcpy (SSE), and increments a mutex-protected counter.
   Any mismatch is reported and the program aborts.  */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define SLOTS	256
#define POOL	64

static int nthreads = 4;
static long iterations = 200000;
/* 1: no shared pool, 2: no memcpy, 4: no mutex counter,
   8: shared state in anonymous mmap memory instead of .data/.bss,
   16: set tcbhead_t.multiple_threads (%fs:0x18) in each worker.  */
static int mode;

struct shared
{
  pthread_mutex_t lock;
  long counter;
  pthread_mutex_t pool_lock;
  struct blk *pool[POOL];
};
static struct shared static_shared = { PTHREAD_MUTEX_INITIALIZER, 0,
				       PTHREAD_MUTEX_INITIALIZER };
static struct shared *sh = &static_shared;
#define lock (sh->lock)
#define counter (sh->counter)
#define pool_lock (sh->pool_lock)
#define pool (sh->pool)

struct blk
{
  size_t size;
  uint64_t tag;
  uint64_t data[];
};

static uint64_t
pat (uint64_t tag, size_t i)
{
  return tag * 0x9e3779b97f4a7c15ULL + i;
}

static struct blk *
make (size_t words, uint64_t tag)
{
  struct blk *b = malloc (sizeof *b + words * 8);
  if (!b)
    abort ();
  b->size = words;
  b->tag = tag;
  for (size_t i = 0; i < words; i++)
    b->data[i] = pat (tag, i);
  return b;
}

static void
check (struct blk *b, const char *where)
{
  for (size_t i = 0; i < b->size; i++)
    if (b->data[i] != pat (b->tag, i))
      {
	fprintf (stderr, "CORRUPT %s: block %p size %zu tag %llx word %zu "
		 "is %llx want %llx\n", where, (void *) b, b->size,
		 (unsigned long long) b->tag, i,
		 (unsigned long long) b->data[i],
		 (unsigned long long) pat (b->tag, i));
	abort ();
      }
}

static void *
worker (void *arg)
{
  long id = (long) arg;
  struct blk *slot[SLOTS] = { 0 };
  uint64_t seq = (uint64_t) id << 48;
#ifdef __x86_64__
  int mt;
  __asm__ volatile ("movl %%fs:0x18, %0" : "=r" (mt));
  if (id == 0)
    fprintf (stderr, "worker multiple_threads=%d\n", mt);
  if (mode & 16)
    __asm__ volatile ("movl $1, %%fs:0x18" ::: "memory");
#endif
  unsigned r = id * 7919 + 1;

  for (long it = 0; it < iterations; it++)
    {
      r = r * 1103515245 + 12345;
      int s = (r >> 8) % SLOTS;
      size_t words = 1 + ((r >> 16) % ((r & 0x100) ? 512 : 8));

      if (slot[s])
	{
	  check (slot[s], "own");
	  if (!(mode & 2) && (r & 7) == 0)
	    {
	      /* Copy it, check the copy.  */
	      struct blk *c = malloc (sizeof *c + slot[s]->size * 8);
	      if (!c)
		abort ();
	      memcpy (c, slot[s], sizeof *c + slot[s]->size * 8);
	      check (c, "copy");
	      free (c);
	    }
	  if (!(mode & 1) && (r & 3) == 1)
	    {
	      /* Swap with the shared pool; free what was there.  */
	      int p = (r >> 4) % POOL;
	      pthread_mutex_lock (&pool_lock);
	      struct blk *old = pool[p];
	      pool[p] = slot[s];
	      pthread_mutex_unlock (&pool_lock);
	      if (old)
		{
		  check (old, "pool");
		  free (old);
		}
	    }
	  else
	    free (slot[s]);
	}
      slot[s] = make (words, ++seq);

      if (!(mode & 4))
	{
	  pthread_mutex_lock (&lock);
	  counter++;
	  pthread_mutex_unlock (&lock);
	}
    }

  for (int s = 0; s < SLOTS; s++)
    if (slot[s])
      {
	check (slot[s], "final");
	free (slot[s]);
      }
  return NULL;
}

int
main (int argc, char **argv)
{
  if (argc > 1)
    nthreads = atoi (argv[1]);
  if (argc > 2)
    iterations = atol (argv[2]);
  if (argc > 3)
    mode = atoi (argv[3]);
  if (mode & 8)
    {
      sh = mmap (NULL, sizeof *sh, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (sh == MAP_FAILED)
	abort ();
      pthread_mutex_init (&lock, NULL);
      pthread_mutex_init (&pool_lock, NULL);
    }

  pthread_t t[64];
  for (long i = 0; i < nthreads; i++)
    pthread_create (&t[i], NULL, worker, (void *) i);
  for (int i = 0; i < nthreads; i++)
    pthread_join (t[i], NULL);

  for (int p = 0; p < POOL; p++)
    if (pool[p])
      {
	check (pool[p], "pool-final");
	free (pool[p]);
      }

  if (!(mode & 4) && counter != (long) nthreads * iterations)
    {
      fprintf (stderr, "COUNTER %ld want %ld\n", counter,
	       (long) nthreads * iterations);
      return 1;
    }
  printf ("MTSTRESS-OK threads %d iterations %ld mode %d\n", nthreads, iterations, mode);
  return 0;
}

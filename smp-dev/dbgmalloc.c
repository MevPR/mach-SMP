/* Debugging allocator for Hurd servers, linked with
   -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc.

   Every block gets a header recording its size and the caller that
   allocated it.  free does not release a block right away: it records
   the caller, fills the block with a pattern and keeps it in a
   quarantine ring.  When a block leaves the ring, the pattern is
   checked: a modified byte means somebody wrote to the block after
   freeing it.  Freeing a block twice is caught too.  Both are reported
   on stderr with the callers involved, and the process then traps so
   that its state can be inspected.  */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

void *__real_malloc (size_t);
void __real_free (void *);
void *__real_realloc (void *, size_t);

#define MAGIC_LIVE  0x4c49564542414c4cUL	/* "LLABEVIL" */
#define MAGIC_FREED 0x4445455246424c4cUL
#define POISON      0xa5
#define RING        65536			/* blocks in quarantine */

struct hdr
{
  uint64_t magic;
  size_t size;
  void *alloc_caller;
  void *free_caller;
};

static struct hdr *ring[RING];
static unsigned long ring_pos;
static pthread_spinlock_t ring_lock;
static int ring_lock_init;

static void
out (const char *s)
{
  write (2, s, strlen (s));
}

static void
outhex (unsigned long v)
{
  char buf[19];
  int i = 18;
  buf[i] = 0;
  do
    {
      buf[--i] = "0123456789abcdef"[v & 15];
      v >>= 4;
    }
  while (v && i > 2);
  buf[--i] = 'x';
  buf[--i] = '0';
  out (buf + i);
}

/* The last report, for post-mortem inspection of a core when stderr
   goes nowhere.  */
struct
{
  const char *what;
  struct hdr *h;
  struct hdr copy;
  size_t off;
  void *caller;
} dbgmalloc_last;

static void
report (const char *what, struct hdr *h, size_t off, void *caller)
{
  dbgmalloc_last.what = what;
  dbgmalloc_last.h = h;
  dbgmalloc_last.copy = *h;
  dbgmalloc_last.off = off;
  dbgmalloc_last.caller = caller;
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
  out ("\ndbgmalloc: ");
  out (what);
  out (" block ");
  outhex ((unsigned long) (h + 1));
  out (" size ");
  outhex (h->size);
  out (" offset ");
  outhex (off);
  out (" allocated by ");
  outhex ((unsigned long) h->alloc_caller);
  out (" freed by ");
  outhex ((unsigned long) h->free_caller);
  out (" now ");
  outhex ((unsigned long) caller);
  out ("\n");
  __builtin_trap ();
}

static void
check_poison (struct hdr *h)
{
  unsigned char *p = (unsigned char *) (h + 1);
  for (size_t i = 0; i < h->size; i++)
    if (p[i] != POISON)
      report ("write after free", h, i, 0);
}

static void
lock (void)
{
  if (! __atomic_load_n (&ring_lock_init, __ATOMIC_ACQUIRE))
    {
      pthread_spin_init (&ring_lock, PTHREAD_PROCESS_PRIVATE);
      __atomic_store_n (&ring_lock_init, 1, __ATOMIC_RELEASE);
    }
  pthread_spin_lock (&ring_lock);
}

void *
__wrap_malloc (size_t size)
{
  struct hdr *h = __real_malloc (sizeof *h + size);
  if (h == NULL)
    return NULL;
  h->magic = MAGIC_LIVE;
  h->size = size;
  h->alloc_caller = __builtin_return_address (0);
  h->free_caller = 0;
  return h + 1;
}

void
__wrap_free (void *ptr)
{
  struct hdr *h, *old;

  if (ptr == NULL)
    return;
  h = (struct hdr *) ptr - 1;
  /* Claim the block atomically, so that two threads freeing it at the
     same time cannot both get past this point.  */
  uint64_t expect = MAGIC_LIVE;
  if (! __atomic_compare_exchange_n (&h->magic, &expect, MAGIC_FREED, 0,
				     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      if (expect == MAGIC_FREED)
	report ("double free of", h, 0, __builtin_return_address (0));
      /* Not ours (e.g. from posix_memalign or glibc internals).  */
      __real_free (ptr);
      return;
    }
  h->free_caller = __builtin_return_address (0);
  memset (ptr, POISON, h->size);

  lock ();
  old = ring[ring_pos % RING];
  ring[ring_pos % RING] = h;
  ring_pos++;
  if (old)
    check_poison (old);
  pthread_spin_unlock (&ring_lock);

  if (old)
    {
      old->magic = 0;
      __real_free (old);
    }
}

void *
__wrap_calloc (size_t n, size_t size)
{
  size_t total;
  void *p;
  if (__builtin_mul_overflow (n, size, &total))
    return NULL;
  p = __wrap_malloc (total);
  if (p)
    memset (p, 0, total);
  return p;
}

void *
__wrap_realloc (void *ptr, size_t size)
{
  struct hdr *h;
  void *p;

  if (ptr == NULL)
    return __wrap_malloc (size);
  h = (struct hdr *) ptr - 1;
  if (h->magic == MAGIC_FREED)
    report ("realloc of freed", h, 0, __builtin_return_address (0));
  if (h->magic != MAGIC_LIVE)
    /* Not ours, e.g. from strdup or asprintf inside libc.  */
    return __real_realloc (ptr, size);
  p = __wrap_malloc (size);
  if (p == NULL)
    return NULL;
  if (__atomic_load_n (&h->magic, __ATOMIC_ACQUIRE) != MAGIC_LIVE)
    report ("realloc of freed", h, 0, __builtin_return_address (0));
  memcpy (p, ptr, h->size < size ? h->size : size);
  ((struct hdr *) p - 1)->alloc_caller = __builtin_return_address (0);
  __wrap_free (ptr);
  return p;
}

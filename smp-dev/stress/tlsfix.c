/* Work around glibc's Hurd _hurd_tls_new not setting
   tcbhead_t.multiple_threads (see patches-hurd/0001-hurd-set-multiple_threads-*):
   without it, catomic_* operations run without lock prefix in all but the
   main thread.  Link statically with -Wl,--wrap=_dl_allocate_tls.  */
#include <stddef.h>

void *__real__dl_allocate_tls (void *);

void *
__wrap__dl_allocate_tls (void *mem)
{
  void *tcb = __real__dl_allocate_tls (mem);
  if (tcb != NULL)
    /* offsetof (tcbhead_t, multiple_threads) on x86_64.  */
    *(int *) ((char *) tcb + 0x18) = 1;
  return tcb;
}

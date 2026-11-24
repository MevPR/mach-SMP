/*
 *  Copyright (C) 2024 Free Software Foundation
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

#include <stdint.h>
#include <mach/machine/thread_status.h>

#include <syscalls.h>
#include <testlib.h>

#include <mach.user.h>

void sleeping_thread(void* arg)
{
  printf("starting thread %d\n", arg);
  for (int i=0; i<100; i++)
      msleep(50);
  printf("stopping thread %d\n", arg);
  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}

void test_many(void)
{
  for (long tid=0; tid<10; tid++)
    {
      test_thread_start(mach_task_self(), sleeping_thread, (void*)tid);
    }
  // TODO: wait for thread end notifications
  msleep(6000);
}

#ifdef __x86_64__
void test_fsgs_base_thread(void* tid)
{
  int err;
#if defined(__SEG_FS) && defined(__SEG_GS)
  long __seg_fs *fs_ptr;
  long __seg_gs *gs_ptr;
  long fs_value;
  long gs_value;

  struct i386_fsgs_base_state state;
  state.fs_base = (unsigned long)&fs_value;
  state.gs_base = (unsigned long)&gs_value;
  err = thread_set_state(mach_thread_self(), i386_FSGS_BASE_STATE,
                         (thread_state_t) &state, i386_FSGS_BASE_STATE_COUNT);
  ASSERT_RET(err, "thread_set_state");

  fs_value = 0x100 + (long)tid;
  gs_value = 0x200 + (long)tid;

  msleep(50);  // allow the others to set their segment base

  fs_ptr = 0;
  gs_ptr = 0;
  long rdvalue = *fs_ptr;
  printf("FS expected %lx read %lx\n", fs_value, rdvalue);
  ASSERT(fs_value == rdvalue, "FS base error\n");
  rdvalue = *gs_ptr;
  printf("GS expected %lx read %lx\n", gs_value, rdvalue);
  ASSERT(gs_value == rdvalue, "GS base error\n");
#else
#error " missing __SEG_FS and __SEG_GS"
#endif

  thread_terminate(mach_thread_self());
  FAILURE("thread_terminate");
}
#endif

void test_fsgs_base(void)
{
#ifdef __x86_64__
  int err;
  for (long tid=0; tid<10; tid++)
    {
      test_thread_start(mach_task_self(), test_fsgs_base_thread, (void*)tid);
    }
  msleep(1000);  // TODO: wait for threads
#endif
}

#ifdef __x86_64__
static void set_fsgs_base_expect_invalid(thread_t thread,
                                         unsigned long fs_base,
                                         unsigned long gs_base)
{
  struct i386_fsgs_base_state bad;
  int err;

  bad.fs_base = fs_base;
  bad.gs_base = gs_base;
  err = thread_set_state(thread, i386_FSGS_BASE_STATE,
                         (thread_state_t) &bad, i386_FSGS_BASE_STATE_COUNT);
  ASSERT(err == KERN_INVALID_ARGUMENT, "invalid fs/gs base accepted\n");
}

/* Negative, non-canonical, and just past the user address space.  */
static const unsigned long bad_bases[] = {
  0xffff800000001000UL, 0x8000000000000000UL, 0x0000800000000000UL,
};
#define N_BAD_BASES (sizeof(bad_bases) / sizeof(bad_bases[0]))
#endif

void test_fsgs_base_invalid(void)
{
#ifdef __x86_64__
  struct i386_fsgs_base_state before, after;
  mach_msg_type_number_t count;
  long __seg_fs *fs_ptr = 0;
  long __seg_gs *gs_ptr = 0;
  long fs_value = 0x1234, gs_value = 0x5678;
  thread_t other;
  int err;

  /* Current thread: rejected values must leave the live bases alone.  */
  before.fs_base = (unsigned long)&fs_value;
  before.gs_base = (unsigned long)&gs_value;
  err = thread_set_state(mach_thread_self(), i386_FSGS_BASE_STATE,
                         (thread_state_t) &before, i386_FSGS_BASE_STATE_COUNT);
  ASSERT_RET(err, "thread_set_state");
  for (int i = 0; i < N_BAD_BASES; i++)
    {
      set_fsgs_base_expect_invalid(mach_thread_self(), 0, bad_bases[i]);
      set_fsgs_base_expect_invalid(mach_thread_self(), bad_bases[i], 0);
      msleep(10);  /* go through a context switch and some interrupts */
      ASSERT(*fs_ptr == fs_value, "FS base changed\n");
      ASSERT(*gs_ptr == gs_value, "GS base changed\n");
    }

  /* Another thread: rejected values must not reach its pcb.  */
  other = test_thread_start(mach_task_self(), sleeping_thread, (void*)100);
  count = i386_FSGS_BASE_STATE_COUNT;
  err = thread_get_state(other, i386_FSGS_BASE_STATE,
                         (thread_state_t) &before, &count);
  ASSERT_RET(err, "thread_get_state");
  for (int i = 0; i < N_BAD_BASES; i++)
    {
      set_fsgs_base_expect_invalid(other, 0, bad_bases[i]);
      set_fsgs_base_expect_invalid(other, bad_bases[i], 0);
    }
  count = i386_FSGS_BASE_STATE_COUNT;
  err = thread_get_state(other, i386_FSGS_BASE_STATE,
                         (thread_state_t) &after, &count);
  ASSERT_RET(err, "thread_get_state");
  ASSERT(before.fs_base == after.fs_base && before.gs_base == after.gs_base,
         "fs/gs base of other thread changed\n");
#endif
}


int main(int argc, char *argv[], int envc, char *envp[])
{
  test_fsgs_base();
  test_fsgs_base_invalid();
  test_many();
  return 0;
}

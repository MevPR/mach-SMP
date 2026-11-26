#!/usr/bin/env python3
"""Binary patches used to test the userland fixes on Debian GNU/Hurd amd64
(image 20250807) without rebuilding the packages.

  binpatch.py <which> <input> <output>

<which> is one of:

  libpthread  libpthread.so.0.3 (glibc 2.41, htl): set
              tcbhead_t.multiple_threads in the TCB of new threads.
              Equivalent of outbox/glibc/0002.  In __pthread_create_internal,
              `tcb->tcb = tcb` (mov %rax,(%rax) at 0x66c3) jumps to a 22-byte
              cave in the alignment gap after .fini (0xc248, still in the
              executable segment) that also does movl $1,0x18(%rax).

  libc-fork   libc.so.0.3 (glibc 2.41): drop the early resume_threads call in
              _Fork (call at 0x10d227 -> 5-byte nop), so that the threads are
              resumed by the later call, after the signal thread state was
              fetched.  Close equivalent of outbox/glibc/0001.

  libc243-fork  libc.so.0.3 (glibc 2.43-7): the same for 2.43, where
              resume_threads is a nested function: its first call, at
              0x131fcc (before the child's __thread_create), becomes a nop.

  libddekit   libmachdevdde.so.0.3 (netdde): drop the `je` at 0x3ee5 that
              makes the timer thread wait without timeout when the next timer
              is due within 1ms.  Equivalent of outbox/netdde/0001.

Every patch checks the original bytes first and refuses other builds.
Static programs carry their own copy of htl: relink them with
stress/tlsfix.c instead (see testpatch/rebuild-statics.sh).
"""
import struct
import sys


def libpthread(b):
    orig = bytes.fromhex('488900' '488b4d98' '4c89ea')
    assert b[0x66c3:0x66cd] == orig, 'unexpected code at 0x66c3'
    assert not any(b[0xc245:0xc260]), 'gap after .fini not empty'
    cave, back = 0xc248, 0x66cd
    code = (bytes.fromhex('488900' 'c7401801000000' '488b4d98' '4c89ea')
            + b'\xe9' + struct.pack('<i', back - (cave + 22)))
    b[cave:cave + 22] = code
    b[0x66c3:0x66c8] = b'\xe9' + struct.pack('<i', cave - (0x66c3 + 5))
    b[0x66c8:0x66cd] = bytes.fromhex('0f1f440000')


def libc_fork(b):
    assert b[0x10d227:0x10d22c] == bytes.fromhex('e8d4eeffff'), \
        'unexpected code at 0x10d227'
    b[0x10d227:0x10d22c] = bytes.fromhex('0f1f440000')


def libc243_fork(b):
    assert b[0x131fc5:0x131fd1] == bytes.fromhex('4c8d9570fdffff' 'e82fedffff'), \
        'unexpected code at 0x131fc5'
    b[0x131fcc:0x131fd1] = bytes.fromhex('0f1f440000')


def libddekit(b):
    assert b[0x3ee3:0x3ee7] == bytes.fromhex('85f67416'), \
        'unexpected code at 0x3ee3'
    b[0x3ee5:0x3ee7] = b'\x90\x90'


PATCHES = {'libpthread': libpthread, 'libc-fork': libc_fork,
           'libc243-fork': libc243_fork,
           'libddekit': libddekit}

if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in PATCHES:
        sys.exit(__doc__)
    data = bytearray(open(sys.argv[2], 'rb').read())
    PATCHES[sys.argv[1]](data)
    open(sys.argv[3], 'wb').write(data)
    print('patched', sys.argv[3])

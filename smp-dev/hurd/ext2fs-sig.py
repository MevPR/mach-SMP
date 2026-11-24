import gdb
exec(open('/hb/uread.py').read())
t = find_task('ext2fs')
stopped = int.from_bytes(uread(t, 0x623698, 4), 'little')
msgport_thread = int.from_bytes(uread(t, 0x623690, 4), 'little')
gss = int.from_bytes(uread(t, 0x623670, 8), 'little')
print("ext2fs task %s: _hurd_stopped=%d _hurd_msgport_thread=%#x _hurd_global_sigstate=%#x" %
      (t, stopped, msgport_thread, gss))
if gss:
    words = [int.from_bytes(uread(t, gss + 8 * i, 8), 'little') for i in range(24)]
    print("global sigstate words: " + " ".join("%#x" % w for w in words))

# Applying the userland fixes to a test system

The source fixes are in `../outbox/` (mail-ready series) and `../patches-hurd/`
(single files).  To test them on Debian GNU/Hurd amd64 (image 20250807)
without rebuilding glibc, netdde or rumpkernel, they were applied like this:

| Fix | How it was applied |
|---|---|
| glibc `multiple_threads` (dynamic programs) | `binpatch.py libpthread` on `/usr/lib/x86_64-gnu/libpthread.so.0.3` |
| glibc `multiple_threads` (static boot servers) | relinked with `../stress/tlsfix.c` and `-Wl,--wrap=_dl_allocate_tls`: `rebuild-tlsfix.sh` (ext2fs), `rebuild-statics.sh` (pci-arbiter, acpi, rumpdisk) |
| glibc fork deadlock | `binpatch.py libc-fork` on `/usr/lib/x86_64-gnu/libc.so.0.3` |
| libddekit timer | `binpatch.py libddekit` on `/usr/lib/x86_64-gnu/libmachdevdde.so.0.3` |
| libports, libfshelp, libpager | hurd 0.9.git20250801 sources patched and rebuilt in the Hurd VM: `rebuild-fixes.sh` (libports in its adapted form, `../patches-hurd/libports-final-deref-for-0.9.git20250801.diff`) |
| pflocal unbind race | `rebuild-pflocal.sh`: patch applied to the Debian source, pflocal rebuilt and installed |
| debugging | `rebuild-dbg.sh`: ext2fs.static with `../dbgmalloc.c` |
| rumpkernel bus_dma | `rebuild-rumpdisk-dma.sh` (rumpdev_bus_dma.c rebuilt alone with gcc 14 and the package flags, verified against Debian's object, swapped into a copy of librumpdev_pci_pic.a) and `relink-rumpdisk.sh`; also `../outbox/rumpkernel/busdma_test.c` |
| console-client SIGPIPE | `rebuild-console.sh` (note `libdir=/usr/lib/x86_64-gnu`, or the drivers are not found) |

The scripts run inside the dev VM (`/root/hurd-0.9.git20250801`, configured
with `--enable-static-progs=ext2fs`); the binary patches run anywhere and
check the original bytes first.  Originals were kept in `/root/orig` on the
test image.

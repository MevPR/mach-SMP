#!/bin/sh
set -e
cd /root/hurd-0.9.git20250801/build
rm -f rumpdisk/rumpdisk.static
make -C rumpdisk rumpdisk.static LDFLAGS="-L/root/rumpfix /root/tlsfix.o -Wl,--wrap=_dl_allocate_tls" > /root/rumpdisk-dma.log 2>&1 || { tail -5 /root/rumpdisk-dma.log; exit 1; }
grep -o "\-L/root/rumpfix" /root/rumpdisk-dma.log | head -1
objdump -d rumpdisk/rumpdisk.static | grep -c "rumpcomp_pci_virt_to_mach>$" || true
install -m 755 rumpdisk/rumpdisk.static /hurd/rumpdisk.static; sync; echo RUMPDISK-OK

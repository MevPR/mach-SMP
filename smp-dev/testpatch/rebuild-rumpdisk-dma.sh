#!/bin/sh
# In the dev VM: rebuild rump's rumpdev_bus_dma object with the bus_dma
# machine-address fix and relink rumpdisk.static against it.
set -e
W=/root/rk; rm -rf $W; mkdir -p $W; cd $W; tar xf /root/rumpsrc.tar
[ -d pci-userspace ] && mkdir -p "rumpkernel-0~20250111/pci-userspace" && cp -r pci-userspace/* "rumpkernel-0~20250111/pci-userspace/"
R="$W/rumpkernel-0~20250111/buildrump.sh/src"
F=$R/sys/rump/dev/lib/libpci/rumpdev_bus_dma.c
rename() {  # prefix kernel symbols with rumpns_, as Makefile.rump does
  nm "$1" | awk '$NF !~ /^(rump|RUMP|__|_GLOBAL_OFFSET_TABLE)/ && NF>=2 {print $NF, "rumpns_" $NF}' | sort -u > $W/syms
  objcopy --redefine-syms=$W/syms "$1"
}
norm() { objdump -d --no-show-raw-insn "$1" | sed -n '/^Disassembly/,$p' | grep -v '^$' | sed -E 's/^ *[0-9a-f]+:\t//; s/ <[^>]*>//; s/^(j[a-z]+|call) +[0-9a-f]+/\1 X/; s/^[0-9a-f]+ /F /'; }
sh /root/rumpcc.sh "$R" gcc $W/orig.o "$F" -include /root/vtophys_decl.h
rename $W/orig.o
mkdir -p $W/deb && cd $W/deb && ar x /usr/lib/librumpdev_pci_pic.a rumpdev_bus_dma.pico && cd $W
norm $W/deb/rumpdev_bus_dma.pico > $W/deb.s; norm $W/orig.o > $W/orig.s
echo "funcs deb: $(nm $W/deb/rumpdev_bus_dma.pico | grep -c ' T ') ours: $(nm $W/orig.o | grep -c ' T ')"
echo "undef diff: $(diff <(nm $W/deb/rumpdev_bus_dma.pico | awk '$1=="U"{print $2}' | sort) <(nm $W/orig.o | awk '$1=="U"{print $2}' | sort) | wc -l) lines"
diff $W/deb.s $W/orig.s > $W/diff.txt || true
echo "insn lines deb $(wc -l < $W/deb.s) ours $(wc -l < $W/orig.s), differing: $(diff $W/deb.s $W/orig.s | grep -c '^[<>]')"
cp /root/rumpdev_bus_dma.fixed.c $F
sh /root/rumpcc.sh "$R" gcc $W/fixed.o "$F" -include /root/vtophys_decl.h
rename $W/fixed.o
echo "fixed funcs: $(nm $W/fixed.o | grep -c ' T ')"
mkdir -p /root/rumpfix && cp /usr/lib/librumpdev_pci_pic.a /root/rumpfix/ && cp $W/fixed.o /root/rumpfix/rumpdev_bus_dma.pico && cd /root/rumpfix && ar r librumpdev_pci_pic.a rumpdev_bus_dma.pico && echo archive-updated

# usage: rumpcc.sh <src-root (buildrump.sh/src)> <cc> <out.o> <file.c> [extra flags]
R=$1; CC=$2; OUT=$3; F=$4; shift 4
INC=$(mktemp -d); ln -s $R/sys/arch/amd64/include $INC/machine; ln -s $R/sys/arch/x86/include $INC/x86; ln -s $R/sys/arch/amd64/include $INC/amd64
$CC -c -o $OUT $F \
  -D_KERNEL -D_RUMPKERNEL -D__NetBSD__ -U__linux__ -U__linux -U__gnu_linux__ -U__GNU__ -U__gnu_hurd__ -U__MACH__ \
  -D_FILE_OFFSET_BITS=64 -DRUMP_REGISTER_T=int -DRUMPUSER_CONFIG=yes -DNO_PCI_MSI_MSIX=yes -DNUSB_DMA=1 -DPAE -DBUFPAGES=16 \
  -DDIAGNOSTIC -DKTRACE -DRUMP_USE_CTOR -DRUMP_CURLWP=RUMP_CURLWP___THREAD \
  -nostdinc -ffreestanding -fno-strict-aliasing -fno-delete-null-pointer-checks \
  -I$R/sys/rump/dev/lib/libpci -I$R/../../pci-userspace/src-gnu -I. -I$R/common/include -I$R/sys/rump/include -I$R/sys/rump/include/opt \
  -I$INC -I$R/sys/arch -I$R/sys -I$R/sys/rump/librump/rumpkern \
  -g -O2 -fno-stack-protector -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -fcommon -fPIC -DPIC "$@"
rc=$?; rm -rf $INC; exit $rc

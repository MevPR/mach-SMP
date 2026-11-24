# usage: mkiso.sh <kernel.gz> <out.iso> "<kernel args>"
set -e
K=$1; OUT=$2; ARGS=$3
D=$(mktemp -d); mkdir -p $D/boot/grub; cp $K $D/boot/gnumach.gz
cat > $D/boot/grub/grub.cfg <<EOG
set timeout=0
serial --unit=0 --speed=115200
terminal_output serial
menuentry hurd {
	insmod part_msdos
	insmod ext2
	multiboot (cd)/boot/gnumach.gz root=part:1:device:wd0 console=com0 $ARGS
	set root=(hd0,msdos1)
	module /hurd/pci-arbiter.static pci-arbiter --host-priv-port='\${host-port}' --device-master-port='\${device-port}' --next-task='\${acpi-task}' '\$(pci-task=task-create)' '\$(task-resume)'
	module /hurd/acpi.static acpi --next-task='\${disk-task}' '\$(acpi-task=task-create)'
	module /hurd/rumpdisk.static rumpdisk --next-task='\${fs-task}' '\$(disk-task=task-create)'
	module /hurd/ext2fs.static ext2fs --readonly --multiboot-command-line='\${kernel-command-line}' --exec-server-task='\${exec-task}' -T typed '\${root}' '\$(fs-task=task-create)'
	module /lib/ld-x86-64.so.1 exec /hurd/exec '\$(exec-task=task-create)'
	boot
}
EOG
grub-mkrescue -o $OUT $D >/dev/null 2>&1
rm -rf $D; ls -la $OUT

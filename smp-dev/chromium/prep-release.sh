#!/bin/sh
# In the image VM: make the image boot the SMP kernel from its own disk,
# clean it up for distribution.
set -e
install -m 644 /root/gnumach-smp-v1.0-x86_64.gz /boot/gnumach-1.8-amd64-smp-v1.0.gz
sed -i 's|^GRUB_CMDLINE_GNUMACH=.*|GRUB_CMDLINE_GNUMACH="smp_pset=default"|; s|^GRUB_TIMEOUT=.*|GRUB_TIMEOUT=3|' /etc/default/grub
grep -q '^GRUB_TOP_LEVEL=' /etc/default/grub || echo 'GRUB_TOP_LEVEL=/boot/gnumach-1.8-amd64-smp-v1.0.gz' >> /etc/default/grub
grep -q '^GRUB_TERMINAL=' /etc/default/grub || echo 'GRUB_TERMINAL="console serial"' >> /etc/default/grub
grep -q '^GRUB_SERIAL_COMMAND=' /etc/default/grub || echo 'GRUB_SERIAL_COMMAND="serial --unit=0 --speed=115200"' >> /etc/default/grub
update-grub 2>&1 | tail -3
grep -m3 "multiboot" /boot/grub/grub.cfg
# clean up
rm -rf /root/hurd-0.9.git20261028 /root/*.log /root/*.out /root/*.patched /root/*.tar.gz /root/hfix /root/hfix-*.sh /root/build-hurdfix.sh /root/ddekit.patch /root/gnumach-smp-v1.0-x86_64.gz
apt-get clean
rm -rf /tmp/* /tmp/.X* /var/tmp/* /root/.cache /root/.config/chromium
find /var/log -type f -exec truncate -s 0 {} \;
rm -f /root/.bash_history
# fresh ssh host keys on first boot of every copy
rm -f /etc/ssh/ssh_host_*
cat > /etc/rc.local <<'RC'
#!/bin/sh
# Generate ssh host keys on the first boot of a copy of the image.
if [ ! -f /etc/ssh/ssh_host_ed25519_key ]; then
  ssh-keygen -A && /etc/init.d/ssh restart
fi
exit 0
RC
chmod 755 /etc/rc.local
cp /root/README-smp.txt /root/README-smp.txt 2>/dev/null || true
sync
df -h / | tail -1
echo PREP-OK

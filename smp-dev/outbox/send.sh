#!/bin/sh
# Send the prepared patch series upstream with git send-email.
#
#   sh send.sh            show what would be sent (dry run)
#   sh send.sh --send     really send
#
# Needs git send-email set up for your mailbox.org account, e.g.:
#   git config --global sendemail.smtpServer smtp.mailbox.org
#   git config --global sendemail.smtpServerPort 465
#   git config --global sendemail.smtpEncryption ssl
#   git config --global sendemail.smtpUser magnus.reimann@mailbox.org
# (git asks for the password, or set up a credential helper.)
set -e
cd "$(dirname "$0")"

if [ "$1" = "--send" ]; then MODE=""; else MODE="--dry-run"; fi

SAMUEL=samuel.thibault@ens-lyon.org
COMMON="$MODE --confirm=never --suppress-cc=self --thread --no-chain-reply-to"

# GNU Mach: SMP fixes, lock debugging, SMP tests (17 patches + cover letter)
git send-email $COMMON --to=bug-hurd@gnu.org --cc=$SAMUEL gnumach/*.patch

# Hurd: libports, libfshelp, libpager races (4 patches + cover letter)
git send-email $COMMON --to=bug-hurd@gnu.org --cc=$SAMUEL hurd/*.patch

# netdde: libddekit timer thread sleeping forever (1 patch)
git send-email $COMMON --to=bug-hurd@gnu.org --cc=$SAMUEL netdde/*.patch

# rumpkernel (Debian package): bus_dma machine contiguity (1 patch)
git send-email $COMMON --to=bug-hurd@gnu.org --cc=$SAMUEL rumpkernel/0001-*.patch

# glibc: multiple_threads in new TCBs, fork/signal thread deadlock
# (2 patches + cover letter), with DCO Signed-off-by lines.
git send-email $COMMON --to=libc-alpha@sourceware.org --cc=bug-hurd@gnu.org \
  --cc=$SAMUEL glibc/*.patch

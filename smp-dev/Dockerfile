FROM debian:sid
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential mig-x86-64-gnu autoconf automake libtool texinfo flex bison gawk \
    git ca-certificates e2fsprogs fdisk qemu-utils python3-pexpect qemu-system-x86 xorriso grub-pc-bin grub-common mtools python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src

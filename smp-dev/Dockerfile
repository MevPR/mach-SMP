FROM debian:sid
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential mig-x86-64-gnu autoconf automake libtool texinfo flex bison gawk \
    git ca-certificates qemu-system-x86 xorriso grub-pc-bin grub-common mtools python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src

# Chromium acceptance test

Goal: on 2 processors (`smp_pset=default`), Chromium from
[chromium-hurd](https://github.com/MevPR/chromium-hurd) opens
https://www.duckduckgo.com and renders it.

Image: Debian GNU/Hurd unstable (November 2026: glibc 2.43, hurd
0.9.git20261028), 8 GB raw disk, Xvfb and the chromium package.  The
early-boot fork hang needs the glibc fork fix
(`../testpatch/binpatch.py libc243-fork`).

| Script | Where | What |
|---|---|---|
| `devvm.py` | host container | boots the image under QEMU; `IMG`, `SMP`, `MEM`, `AHCI=1`, `QEMU_EXTRA` from the environment |
| `gdb-catch-assert.sh` | host container | attaches gdb to QEMU's gdbstub, stops on `Assert`/`Panic`, writes all backtraces |
| `chrome-test.sh <secs> <tag>` | VM | Chromium in Xvfb, NetLog, screenshot every 5 min; `EXTRA` adds flags |
| `chrome-headless.sh`, `chrome-netlog.sh` | VM | headless `--dump-dom` run (with NetLog) |
| `utilwrap.sh` | VM | `--utility-cmd-prefix` that logs how utility processes end |
| `hurdfix-build.sh` | VM | rebuilds Debian's hurd source with the hurd-SMP fixes and installs them |

Lessons:

- Use AHCI (`AHCI=1`): with PIIX IDE under TCG and 4 GB, ext2fs gets I/O
  errors.
- The Hurd sets `RLIMIT_AS` to the RAM size; PartitionAlloc needs
  `ulimit -v unlimited`.
- Xvfb has no OpenGL: the GPU process exits and Chromium falls back to
  software compositing; harmless.
- Everything is slow under TCG: the DuckDuckGo document is requested
  about 6 minutes after start.
